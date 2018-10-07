#pragma once
#include <memory>
#include <string>

/* These match mesa's internal stages */
enum class nx_shader_stage
{
    NONE = -1,
    VERTEX = 0,
    TESS_CTRL = 1,
    TESS_EVAL = 2,
    GEOMETRY = 3,
    FRAGMENT = 4,
    COMPUTE = 5,
};

struct standalone_options
{
    int glsl_version;
    int dump_ast;
    int dump_hir;
    int dump_lir;
    int dump_builder;
    int do_link;
    int just_log;
};

class nx_compiler;
class nx_shader_stage_object
{
    friend class nx_compiler;
    nx_compiler* m_parent = nullptr;
    struct gl_shader *m_shader = nullptr;
    nx_shader_stage_object(nx_compiler& parent) : m_parent(&parent) {}
public:
    nx_shader_stage_object() = default;
    nx_shader_stage_object(const nx_shader_stage_object&);
    nx_shader_stage_object& operator=(const nx_shader_stage_object&);
    ~nx_shader_stage_object() { reset(); }
    void reset();
    operator bool() const;
    nx_shader_stage stage() const;
    const char* info_log() const;
};

class nx_linked_shader
{
    friend class nx_compiler;
    nx_compiler* m_parent = nullptr;
    struct gl_shader_program* m_program = nullptr;
    nx_linked_shader(nx_compiler& parent) : m_parent(&parent) {}
public:
    nx_linked_shader() = default;
    nx_linked_shader(const nx_linked_shader&);
    nx_linked_shader& operator=(const nx_linked_shader&);
    ~nx_linked_shader() { reset(); }
    void reset();
    operator bool() const { return m_program != nullptr; }
    const struct gl_shader_program* program() const { return m_program; }
};

class nx_compiler
{
    friend class nx_shader_stage_object;
    friend class nx_linked_shader;
    struct pipe_screen *m_screen = nullptr;
    struct st_context *m_st = nullptr;
    struct standalone_options m_options = {};
    bool m_ownsCtx = false;
    void compile_shader(struct gl_context *ctx, struct gl_shader *shader);
public:
    nx_compiler();
    ~nx_compiler();
    bool initialize(struct pipe_screen *screen, struct st_context *st,
                    const struct standalone_options *o = nullptr);
    bool initialize(const struct standalone_options *o = nullptr);
    nx_shader_stage_object compile(nx_shader_stage type, const char *source);
    nx_linked_shader link(unsigned num_stages, const nx_shader_stage_object **stages, std::string* infoLog = nullptr);
    std::pair<std::shared_ptr<uint8_t[]>, size_t>
    offline_link(unsigned num_stages, const nx_shader_stage_object **stages, std::string* infoLog = nullptr);
};
