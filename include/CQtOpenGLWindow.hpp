#ifndef CQTOPENGLWINDOW_HPP
#define CQTOPENGLWINDOW_HPP

#include <QWindow>
#include <QOpenGLFunctions>
#include <ISurface.hpp>

class CQtOpenGLWindow : public QWindow, ISurface
{
    Q_OBJECT
public:
    explicit CQtOpenGLWindow(QWindow *parent = 0);
    CQtOpenGLWindow();
    
    virtual void render();
    
    virtual void initialize();
    
    
    public slots:
    void renderLater();
    void renderNow();
    
protected:
    bool event(QEvent *event) Q_DECL_OVERRIDE;
    
    void exposeEvent(QExposeEvent *event) Q_DECL_OVERRIDE;
    
private:
    bool m_update_pending;
    bool m_animating;
    
    QOpenGLContext *m_context;
};

#endif // CQTOPENGLWINDOW_HPP
