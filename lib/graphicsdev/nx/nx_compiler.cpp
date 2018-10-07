#include "boo/graphicsdev/nx_compiler.hpp"

/*
 * Copyright © 2008, 2009 Intel Corporation
 * Boo Modifications © 2018 Jack Andersen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/** @file nx_compiler.cpp
 *
 * Based on standalone.cpp in compiler/glsl. This file provides a means to
 * compile and link GLSL sources directly into driver programs for the
 * Nouveau GM107 chipset configuration.
 */

#include "ast.h"
#include "glsl_parser_extras.h"
#include "ir_optimization.h"
#include "program.h"
#include "loop_analysis.h"
#include "string_to_uint_map.h"
#include "util/set.h"
#include "linker.h"
#include "ir_builder_print_visitor.h"
#include "builtin_functions.h"
#include "opt_add_neg_to_sub.h"

#include "main/shaderobj.h"
#include "st_program.h"

extern "C" {
#include "nouveau_winsys.h"
#include "nouveau_screen.h"
#include "nvc0/nvc0_program.h"
}

_GLAPI_EXPORT __thread void * _glapi_tls_Context;
_GLAPI_EXPORT __thread struct _glapi_table * _glapi_tls_Dispatch;

int
_glapi_add_dispatch( const char * const * function_names,
                     const char * parameter_signature )
{
    return 0;
}

void
_glapi_destroy_multithread(void)
{
}

void
_glapi_check_multithread(void)
{
}

void
_glapi_set_context(void *context)
{
    _glapi_tls_Context = context;
}

void *
_glapi_get_context()
{
    return _glapi_tls_Context;
}

void
_glapi_set_dispatch(struct _glapi_table *dispatch)
{
    _glapi_tls_Dispatch = dispatch;
}

struct _glapi_table *
_glapi_get_dispatch()
{
    return _glapi_tls_Dispatch;
}

GLuint
_glapi_get_dispatch_table_size(void)
{
    /*
     * The dispatch table size (number of entries) is the size of the
     * _glapi_table struct plus the number of dynamic entries we can add.
     * The extra slots can be filled in by DRI drivers that register new
     * extension functions.
     */
    return 0;
}

class dead_variable_visitor : public ir_hierarchical_visitor {
public:
    dead_variable_visitor()
    {
        variables = _mesa_set_create(NULL,
                                     _mesa_hash_pointer,
                                     _mesa_key_pointer_equal);
    }

    virtual ~dead_variable_visitor()
    {
        _mesa_set_destroy(variables, NULL);
    }

    virtual ir_visitor_status visit(ir_variable *ir)
    {
        /* If the variable is auto or temp, add it to the set of variables that
         * are candidates for removal.
         */
        if (ir->data.mode != ir_var_auto && ir->data.mode != ir_var_temporary)
            return visit_continue;

        _mesa_set_add(variables, ir);

        return visit_continue;
    }

    virtual ir_visitor_status visit(ir_dereference_variable *ir)
    {
        struct set_entry *entry = _mesa_set_search(variables, ir->var);

        /* If a variable is dereferenced at all, remove it from the set of
         * variables that are candidates for removal.
         */
        if (entry != NULL)
            _mesa_set_remove(variables, entry);

        return visit_continue;
    }

    void remove_dead_variables()
    {
        struct set_entry *entry;

        set_foreach(variables, entry) {
            ir_variable *ir = (ir_variable *) entry->key;

            assert(ir->ir_type == ir_type_variable);
            ir->remove();
        }
    }

private:
    set *variables;
};

void nx_compiler::compile_shader(struct gl_context *ctx, struct gl_shader *shader)
{
    struct _mesa_glsl_parse_state *state =
        new(shader) _mesa_glsl_parse_state(ctx, shader->Stage, shader);

    _mesa_glsl_compile_shader(ctx, shader, m_options.dump_ast,
                              m_options.dump_hir, true);

    /* Print out the resulting IR */
    if (!state->error && m_options.dump_lir) {
        _mesa_print_ir(stdout, shader->ir, state);
    }
}

nx_compiler::nx_compiler()
{
    m_options.glsl_version = 330;
    m_options.do_link = true;
}

nx_compiler::~nx_compiler()
{
    if (m_ownsCtx)
    {
        _mesa_glsl_release_types();
        _mesa_glsl_release_builtin_functions();
        if (m_st)
            st_destroy_context(m_st);
        if (m_screen)
            m_screen->destroy(m_screen);
    }
}

bool nx_compiler::initialize(struct pipe_screen *screen, struct st_context *st,
                             const struct standalone_options *o)
{
    m_screen = screen;
    m_st = st;
    if (o)
        memcpy(&m_options, o, sizeof(*o));
    return true;
}

bool nx_compiler::initialize(const struct standalone_options* o)
{
    m_ownsCtx = true;
    bool glsl_es;

    if (o)
        memcpy(&m_options, o, sizeof(*o));

    switch (m_options.glsl_version) {
    case 100:
    case 300:
        glsl_es = true;
        break;
    case 110:
    case 120:
    case 130:
    case 140:
    case 150:
    case 330:
    case 400:
    case 410:
    case 420:
    case 430:
    case 440:
    case 450:
    case 460:
        glsl_es = false;
        break;
    default:
        fprintf(stderr, "Unrecognized GLSL version `%d'\n", m_options.glsl_version);
        return false;
    }

    gl_api use_api;
    if (glsl_es) {
        use_api = API_OPENGLES2;
    } else {
        use_api = m_options.glsl_version > 130 ? API_OPENGL_CORE : API_OPENGL_COMPAT;
    }

    struct nouveau_screen *(*init)(struct nouveau_device *);

    struct nouveau_drm *fakedrm = (struct nouveau_drm *)malloc(sizeof(struct nouveau_drm));
    if (!fakedrm)
        return false;
    memset(fakedrm, 0, sizeof(*fakedrm));
    nouveau_device *ndev;
    if (nouveau_device_new(&fakedrm->client, 0, nullptr, 0, &ndev))
        return false;

    switch (ndev->chipset & ~0xf) {
#if 0
    case 0x30:
    case 0x40:
    case 0x60:
        init = nv30_screen_create;
        break;
    case 0x50:
    case 0x80:
    case 0x90:
    case 0xa0:
        init = nv50_screen_create;
        break;
#endif
    default:
    case 0xc0:
    case 0xd0:
    case 0xe0:
    case 0xf0:
    case 0x100:
    case 0x110:
    case 0x120:
    case 0x130:
        init = nvc0_screen_create;
        break;
    }

    struct nouveau_screen *screen = init(ndev);
    if (!screen)
        return false;
    screen->refcount = 1;
    struct pipe_context *p_ctx = screen->base.context_create(&screen->base, nullptr, 0);
    if (!p_ctx)
    {
        screen->base.destroy(&screen->base);
        return false;
    }

    st_config_options opts = {};
    struct st_context *st = st_create_context(use_api, p_ctx, nullptr, nullptr, &opts, false);
    if (!st)
    {
        screen->base.destroy(&screen->base);
        return false;
    }

    return initialize(&screen->base, st);
}

nx_shader_stage_object::nx_shader_stage_object(const nx_shader_stage_object& other)
: m_parent(other.m_parent)
{
    if (!other.m_shader || !m_parent)
        return;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader(ctx, &m_shader, other.m_shader);
}

nx_shader_stage_object& nx_shader_stage_object::operator=(const nx_shader_stage_object& other)
{
    m_parent = other.m_parent;
    if (!other.m_shader || !m_parent)
        return *this;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader(ctx, &m_shader, other.m_shader);
    return *this;
}

void nx_shader_stage_object::reset()
{
    if (!m_shader || !m_parent)
        return;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader(ctx, &m_shader, nullptr);
}

nx_shader_stage_object::operator bool() const
{
    if (!m_shader)
        return false;
    return m_shader->CompileStatus;
}

nx_shader_stage nx_shader_stage_object::stage() const
{
    return nx_shader_stage(m_shader->Stage);
}

const char* nx_shader_stage_object::info_log() const
{
    if (!m_shader)
        return nullptr;
    return m_shader->InfoLog;
}

nx_linked_shader::nx_linked_shader(const nx_linked_shader& other)
: m_parent(other.m_parent)
{
    if (!other.m_program || !m_parent)
        return;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader_program(ctx, &m_program, other.m_program);
}

nx_linked_shader& nx_linked_shader::operator=(const nx_linked_shader& other)
{
    m_parent = other.m_parent;
    if (!other.m_program || !m_parent)
        return *this;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader_program(ctx, &m_program, other.m_program);
    return *this;
}

void nx_linked_shader::reset()
{
    if (!m_program || !m_parent)
        return;
    struct gl_context *ctx = m_parent->m_st->ctx;
    _mesa_reference_shader_program(ctx, &m_program, nullptr);
}

nx_shader_stage_object nx_compiler::compile(nx_shader_stage type, const char *source)
{
    struct gl_context *ctx = m_st->ctx;

    nx_shader_stage_object ret(*this);
    ret.m_shader = rzalloc(nullptr, gl_shader);
    assert(ret.m_shader != NULL);
    ret.m_shader->RefCount = 1;

    ret.m_shader->Stage = gl_shader_stage(type);
    ret.m_shader->Source = source;

    compile_shader(ctx, ret.m_shader);

    /* Mesa doesn't actually own the source, so take it away here */
    ret.m_shader->Source = nullptr;

    return ret;
}

nx_linked_shader nx_compiler::link(unsigned num_stages, const nx_shader_stage_object **stages, std::string* infoLog)
{
    nx_linked_shader ret(*this);
    int status = EXIT_SUCCESS;
    struct gl_context *ctx = m_st->ctx;

    struct gl_shader_program *whole_program;

    whole_program = rzalloc (NULL, struct gl_shader_program);
    assert(whole_program != NULL);
    whole_program->Type = GL_SHADER_PROGRAM_MESA;
    whole_program->data = rzalloc(whole_program, struct gl_shader_program_data);
    assert(whole_program->data != NULL);
    whole_program->data->RefCount = 1;
    whole_program->data->InfoLog = ralloc_strdup(whole_program->data, "");
    ret.m_program = whole_program;

    whole_program->Shaders = (struct gl_shader **)calloc(num_stages, sizeof(struct gl_shader *));
    assert(whole_program->Shaders != NULL);

    for (unsigned i = 0; i < num_stages; i++) {
        whole_program->Shaders[whole_program->NumShaders] = stages[i]->m_shader;
        stages[i]->m_shader->RefCount++;
        whole_program->NumShaders++;

        if (!stages[i]->m_shader->CompileStatus) {
            status = EXIT_FAILURE;
            break;
        }
    }

    if (status == EXIT_SUCCESS) {
        _mesa_clear_shader_program_data(ctx, whole_program);

        if (m_options.do_link)  {
            link_shaders(ctx, whole_program);
            for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
                if (whole_program->_LinkedShaders[i])
                    whole_program->_LinkedShaders[i]->Program->Target = _mesa_shader_stage_to_program(i);
            }
        } else {
            const gl_shader_stage stage = whole_program->Shaders[0]->Stage;

            whole_program->data->LinkStatus = LINKING_SUCCESS;
            whole_program->_LinkedShaders[stage] =
                link_intrastage_shaders(whole_program /* mem_ctx */,
                                        ctx,
                                        whole_program,
                                        whole_program->Shaders,
                                        1,
                                        true);
            whole_program->_LinkedShaders[stage]->Program->Target = _mesa_shader_stage_to_program(stage);

            /* Par-linking can fail, for example, if there are undefined external
             * references.
             */
            if (whole_program->_LinkedShaders[stage] != NULL) {
                assert(whole_program->data->LinkStatus);

                struct gl_shader_compiler_options *const compiler_options =
                    &ctx->Const.ShaderCompilerOptions[stage];

                exec_list *const ir =
                    whole_program->_LinkedShaders[stage]->ir;

                bool progress;
                do {
                    progress = do_function_inlining(ir);

                    progress = do_common_optimization(ir,
                                                      false,
                                                      false,
                                                      compiler_options,
                                                      true)
                               && progress;
                } while(progress);
            }
        }

        status = (whole_program->data->LinkStatus) ? EXIT_SUCCESS : EXIT_FAILURE;

        if (infoLog)
            *infoLog = whole_program->data->InfoLog;

        if (status == EXIT_SUCCESS) {
            for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
                struct gl_linked_shader *shader = whole_program->_LinkedShaders[i];

                if (!shader)
                    continue;

                add_neg_to_sub_visitor v;
                visit_list_elements(&v, shader->ir);

                dead_variable_visitor dv;
                visit_list_elements(&dv, shader->ir);
                dv.remove_dead_variables();
            }

            if (m_options.dump_builder) {
                for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
                    struct gl_linked_shader *shader = whole_program->_LinkedShaders[i];

                    if (!shader)
                        continue;

                    _mesa_print_builder_for_ir(stdout, shader->ir);
                }
            }

            ctx->_Shader = &ctx->Shader;
            st_link_shader(ctx, whole_program);
            ctx->_Shader = nullptr;
            return ret;
        }
    }

    return nx_linked_shader(*this);
}

static void
SizeProgramBuffer(const nvc0_program *prog, size_t &sz)
{
    sz += 140;
    sz += prog->code_size;
}

template<class T> static void
OutputField(T f, uint8_t *&ptr)
{
    memcpy(ptr, &f, sizeof(f));
    ptr += sizeof(f);
}

static void
BuildProgramBuffer(const nvc0_program *prog, uint8_t *&ptr)
{
    OutputField(prog->type, ptr);
    OutputField(prog->translated, ptr);
    OutputField(prog->need_tls, ptr);
    OutputField(prog->num_gprs, ptr);
    OutputField<uint32_t>(prog->code_base, ptr);
    OutputField<uint32_t>(prog->code_size, ptr);
    OutputField<uint32_t>(prog->parm_size, ptr);
    for (const auto& h : prog->hdr)
        OutputField(h, ptr);
    for (const auto& h : prog->flags)
        OutputField(h, ptr);
    OutputField(prog->vp.clip_mode, ptr);
    OutputField(prog->vp.clip_enable, ptr);
    OutputField(prog->vp.cull_enable, ptr);
    OutputField(prog->vp.num_ucps, ptr);
    OutputField(prog->vp.edgeflag, ptr);
    OutputField(prog->vp.need_vertex_id, ptr);
    OutputField(prog->vp.need_draw_parameters, ptr);
    OutputField(prog->fp.early_z, ptr);
    OutputField(prog->fp.colors, ptr);
    OutputField(prog->fp.color_interp[0], ptr);
    OutputField(prog->fp.color_interp[1], ptr);
    OutputField(prog->fp.sample_mask_in, ptr);
    OutputField(prog->fp.force_persample_interp, ptr);
    OutputField(prog->fp.flatshade, ptr);
    OutputField(prog->fp.reads_framebuffer, ptr);
    OutputField(prog->fp.post_depth_coverage, ptr);
    OutputField(prog->tp.tess_mode, ptr);
    OutputField(prog->tp.input_patch_size, ptr);
    OutputField(prog->cp.lmem_size, ptr);
    OutputField(prog->cp.smem_size, ptr);
    OutputField(prog->num_barriers, ptr);
    memcpy(ptr, prog->code, prog->code_size);
    ptr += prog->code_size;
}

std::pair<std::shared_ptr<uint8_t[]>, size_t>
nx_compiler::offline_link(unsigned num_stages, const nx_shader_stage_object **stages, std::string *infoLog)
{
    std::pair<std::shared_ptr<uint8_t[]>, size_t> ret = {};
    auto whole_program = link(num_stages, stages, infoLog);
    if (!whole_program)
        return ret;

    for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
        struct gl_linked_shader *shader = whole_program.m_program->_LinkedShaders[i];
        if (!shader)
            continue;
        struct gl_program *prog = shader->Program;

        switch (prog->Target) {
        case GL_VERTEX_PROGRAM_ARB: {
            struct st_vertex_program *p = (struct st_vertex_program *)prog;
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            SizeProgramBuffer(dp, ret.second);
            break;
        }
        case GL_TESS_CONTROL_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            SizeProgramBuffer(dp, ret.second);
            break;
        }
        case GL_TESS_EVALUATION_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            SizeProgramBuffer(dp, ret.second);
            break;
        }
        case GL_GEOMETRY_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            SizeProgramBuffer(dp, ret.second);
            break;
        }
        case GL_FRAGMENT_PROGRAM_ARB: {
            struct st_fragment_program *p = (struct st_fragment_program *)prog;
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            SizeProgramBuffer(dp, ret.second);
            break;
        }
        default:
            assert(0);
        }
    }

    ret.first.reset(new uint8_t[ret.second]);
    uint8_t *pbuf = ret.first.get();

    for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
        struct gl_linked_shader *shader = whole_program.m_program->_LinkedShaders[i];
        if (!shader)
            continue;
        struct gl_program *prog = shader->Program;

        switch (prog->Target) {
        case GL_VERTEX_PROGRAM_ARB: {
            struct st_vertex_program *p = (struct st_vertex_program *)prog;
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            BuildProgramBuffer(dp, pbuf);
            break;
        }
        case GL_TESS_CONTROL_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            BuildProgramBuffer(dp, pbuf);
            break;
        }
        case GL_TESS_EVALUATION_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            BuildProgramBuffer(dp, pbuf);
            break;
        }
        case GL_GEOMETRY_PROGRAM_NV: {
            struct st_common_program *p = st_common_program(prog);
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            BuildProgramBuffer(dp, pbuf);
            break;
        }
        case GL_FRAGMENT_PROGRAM_ARB: {
            struct st_fragment_program *p = (struct st_fragment_program *)prog;
            nvc0_program *dp = (nvc0_program *)p->variants->driver_shader;
            BuildProgramBuffer(dp, pbuf);
            break;
        }
        default:
            assert(0);
        }
    }

    return ret;
}
