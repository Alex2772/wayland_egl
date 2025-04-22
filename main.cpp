#include <EGL/egl.h>
#include <GL/gl.h>
#include <GLES2/gl2.h>
#include <cassert>
#include <cstdio>
#include <string.h>
#include <string_view>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-generated-protocols/wayland.xml.h>
#include <wayland-generated-protocols/xdg-shell.xml.h>

#if USE_VIEWPORTER_PROTOCOL
#include <wayland-generated-protocols/viewporter.xml.h>
#endif

#if USE_FRACTIONAL_SCALE_PROTOCOL
#include <wayland-generated-protocols/fractional-scale-v1.xml.h>
#endif

#if USE_DECORATION_PROTOCOL
#include <wayland-generated-protocols/xdg-decoration-unstable-v1.xml.h>
#endif


struct client_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_egl_window *egl_window;

    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;

    int32_t logical_width;
    int32_t logical_height;
    int32_t physical_width;
    int32_t physical_height;
    uint8_t running;

#if USE_VIEWPORTER_PROTOCOL
    wp_viewporter* viewporter = nullptr;
    wp_viewport* viewport = nullptr;
#endif

#if USE_FRACTIONAL_SCALE_PROTOCOL
    wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;
    wp_fractional_scale_v1* fractional_scale = nullptr;
#endif
    uint32_t fractional_scale_factor_120 = 120; // 120 = 100%

#if USE_DECORATION_PROTOCOL
    zxdg_decoration_manager_v1* decoration_manager = nullptr;
    zxdg_toplevel_decoration_v1* decoration = nullptr;
#endif
};

/******************************/
/***********Registry***********/
/******************************/

static const struct wl_registry_listener registry_listener = {
    .global = [](void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
        auto state = (client_state*)data;

        auto interface2 = std::string_view(interface);

        if(interface2 == wl_compositor_interface.name) {
            state->compositor = (wl_compositor*)wl_registry_bind(wl_registry, name,
                                                                  &wl_compositor_interface, version);
            return;
        }
        if (interface2 == xdg_wm_base_interface.name) {
            state->xdg_wm_base = (xdg_wm_base*)wl_registry_bind(wl_registry, name,
                                                                &xdg_wm_base_interface, version);
            return;
        }

#if USE_VIEWPORTER_PROTOCOL
        if (interface2 == wp_viewporter_interface.name) {
            state->viewporter = (wp_viewporter*)wl_registry_bind(wl_registry, name,
                                                                 &wp_viewporter_interface, version);
        }
#endif

#if USE_FRACTIONAL_SCALE_PROTOCOL
        if (interface2 == wp_fractional_scale_manager_v1_interface.name) {
            state->fractional_scale_manager = (wp_fractional_scale_manager_v1*)wl_registry_bind(wl_registry, name,
                                                                                                 &wp_fractional_scale_manager_v1_interface, version);
            return;
        }
#endif

#if USE_DECORATION_PROTOCOL
        if (interface2 == zxdg_decoration_manager_v1_interface.name) {
            state->decoration_manager = (zxdg_decoration_manager_v1*) wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, version);
        }
#endif
    },

    .global_remove = [](void* data, struct wl_registry* wl_registry, uint32_t name) {
        (void) data;
        (void) wl_registry;
        (void) name;
    },
};

/******************************/
/******XDG Window Manager******/
/******************************/

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = [](void *data, struct xdg_wm_base *xdg_wm_base,
               uint32_t serial) {
        (void) data;
        xdg_wm_base_pong(xdg_wm_base, serial);
    },
};

/******************************/
/*********XDG Surface**********/
/******************************/

static const struct xdg_surface_listener surface_listener = {
    .configure = [](void *data, struct xdg_surface *xdg_surface,
                    uint32_t serial) {

      auto state = (client_state*)data;

#if USE_FRACTIONAL_SCALE_PROTOCOL
      // round logical by scale factor to ensure pixel perfect.
      state->logical_width = state->logical_width * state->fractional_scale_factor_120 / 120 * 120 / state->fractional_scale_factor_120;
      state->logical_height = state->logical_height * state->fractional_scale_factor_120 / 120 * 120 / state->fractional_scale_factor_120;
#endif

      state->physical_width = int32_t(uint32_t(state->logical_width) * state->fractional_scale_factor_120 / 120);
      state->physical_height = int32_t(uint32_t(state->logical_height) * state->fractional_scale_factor_120 / 120);

#if USE_VIEWPORTER_PROTOCOL
      if (state->viewport) {
          wp_viewport_set_destination(state->viewport,
                                      state->logical_width,
                                      state->logical_height);
      }
#endif


      wl_egl_window_resize(state->egl_window, state->physical_width, state->physical_height, 0, 0);
      printf("glViewport (physical) width=%d, height=%d; (logical %d %d)\n", state->physical_width, state->physical_height, state->logical_width, state->logical_height);
      glViewport(0, 0, state->physical_width, state->physical_height);
      printf("\n");

      xdg_surface_ack_configure(xdg_surface, serial);
    },
};

/******************************/
/********XDG Toplevel**********/
/******************************/

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = [](void *data, struct xdg_toplevel *xdg_toplevel,
                    int32_t width, int32_t height, struct wl_array *states) {
        printf("xdg_toplevel_listener.configure width=%d, height=%d\n", width, height);
        auto state = (client_state*)data;
        if (width == 0 || height == 0) return;
        state->logical_width = width;
        state->logical_height = height;

    },
    .close = [](void *data, struct xdg_toplevel *xdg_toplevel) {
        printf("xdg_toplevel_listener.close\n");
        (void) xdg_toplevel;
        auto state = static_cast<client_state*>(data);

        state->running = 0;
    },
    .configure_bounds = [](void*, xdg_toplevel*, int32_t width, int32_t height) {
        printf("xdg_toplevel_listener.configure width=%d, height=%d\n", width, height);

    },
    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
};

/******************************/
/******************************/
/******************************/

static void wayland_connect(struct client_state *state) {
    state->display = wl_display_connect(nullptr);
    if(!state->display) {
        fprintf(stderr, "Couldn't connect to wayland display\n");
        exit(EXIT_FAILURE);
    }

    state->registry = wl_display_get_registry(state->display);
    wl_registry_add_listener(state->registry, &registry_listener, state);
    wl_display_roundtrip(state->display);
    if(!state->compositor || !state->xdg_wm_base) {
        fprintf(stderr, "Couldn't find compositor or xdg shell\n");
        exit(EXIT_FAILURE);
    }

    xdg_wm_base_add_listener(state->xdg_wm_base, &wm_base_listener, nullptr);

    state->surface = wl_compositor_create_surface(state->compositor);
    state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base,
                                                     state->surface);
    xdg_surface_add_listener(state->xdg_surface, &surface_listener, state);
    state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
    xdg_toplevel_set_title(state->xdg_toplevel, "Hello World");
    xdg_toplevel_add_listener(state->xdg_toplevel, &toplevel_listener, state);

#if USE_VIEWPORTER_PROTOCOL
    if (state->viewporter) {
        state->viewport = wp_viewporter_get_viewport(state->viewporter, state->surface);
        wp_viewport_set_destination(state->viewport, state->logical_width, state->logical_height);
    }
#endif

#if USE_FRACTIONAL_SCALE_PROTOCOL
    if (state->fractional_scale_manager) {
        state->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(state->fractional_scale_manager, state->surface);
        static wp_fractional_scale_v1_listener listener = {
            .preferred_scale = [](void* data, wp_fractional_scale_v1*, uint32_t scale) {
                printf("(wp_fractional_scale_v1_listener) received scale = %f (raw scale = %d)\n", float(scale) / 120.f, scale);
                auto state = static_cast<client_state*>(data);
                state->fractional_scale_factor_120 = scale;
            },
        };
        wp_fractional_scale_v1_add_listener(state->fractional_scale, &listener, state);
    }
#endif

#if USE_DECORATION_PROTOCOL
    if (state->decoration_manager) {
        state->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(state->decoration_manager, state->xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(state->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
#endif

    wl_surface_commit(state->surface);
}

static void egl_init(struct client_state *state) {
    EGLint major;
    EGLint minor;
    EGLint num_configs;
    EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_NONE
    };

    state->egl_window = wl_egl_window_create(state->surface, state->logical_width,
                                             state->logical_height);

    state->egl_display = eglGetDisplay((EGLNativeDisplayType) state->display);
    if(state->display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Couldn't get EGL display\n");
        exit(EXIT_FAILURE);
    }

    if(eglInitialize(state->egl_display, &major, &minor) != EGL_TRUE) {
        fprintf(stderr, "Couldnt initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    if(eglChooseConfig(state->egl_display, attribs, &state->egl_config, 1,
                        &num_configs) != EGL_TRUE) {
        fprintf(stderr, "Couldn't find matching EGL config\n");
        exit(EXIT_FAILURE);
    }

    state->egl_surface = eglCreateWindowSurface(state->egl_display,
                                                state->egl_config,
                                                (EGLNativeWindowType) state->egl_window, nullptr);
    if(state->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Couldn't create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    state->egl_context = eglCreateContext(state->egl_display, state->egl_config,
                                          EGL_NO_CONTEXT, nullptr);
    if(state->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Couldn't create EGL context\n");
        exit(EXIT_FAILURE);
    }

    if(!eglMakeCurrent(state->egl_display, state->egl_surface,
                        state->egl_surface, state->egl_context)) {
        fprintf(stderr, "Couldn't make EGL context current\n");
        exit(EXIT_FAILURE);
    }
}
static GLuint create_shader(const char* source, GLenum shader_type) {
    GLuint shader;
    GLint status;

    shader = glCreateShader(shader_type);
    assert(shader != 0);

    glShaderSource(shader, 1, (const char**)&source, nullptr);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1000];
        GLsizei len;
        glGetShaderInfoLog(shader, 1000, &len, log);
        fprintf(stderr, "Error: compiling %s: %*s\n",
                shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment", len, log);
        exit(1);
    }

    return shader;
}

int main(int argc, char const *argv[]) {
    struct client_state state;

    state.logical_width = 800;
    state.logical_height = 800;
    state.running = 1;

    wayland_connect(&state);

    egl_init(&state);

    // texture
    GLuint texture;
    {
        glGenTextures(1, &texture);

        // 2x2 Image, 3 bytes per pixel (R, G, B)
        GLubyte pixels[4 * 3] = {
            0  , 0  , 0  ,
            255, 255, 255,
            255, 255, 255,
            0  , 0  , 0  ,
        };

        // When texture data is uploaded via glTexImage2D, the rows of pixels are
        // assumed to be aligned to the value set for GL_UNPACK_ALIGNMENT.
        // Use tightly packed data.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Bind the texture object
        glBindTexture(GL_TEXTURE_2D, texture);

        // Load the texture
        // target
        // level
        // internalFormat
        // width: the width of the image in pixels.
        // height:
        // border: ignored in OpenGL ES.
        // format:
        // type: the type of the incoming pixel data.
        // pixels: contains the actual pixel data for the image.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     pixels);

        // Set the minification and magnification filtering mode.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    // shader
    GLuint program, vert, frag;
    {
        const char* vert_shader_text =
                "#version 300 es                            \n"
                "layout(location = 0) in vec4 a_position;   \n"
                "layout(location = 1) in vec2 a_texCoord;   \n"
                "out vec2 v_texCoord;                       \n"
                "void main()                                \n"
                "{                                          \n"
                "   gl_Position = a_position;               \n"
                "   v_texCoord = a_texCoord;                \n"
                "}                                          \n";

        const char* frag_shader_text =
                "#version 300 es                                       \n"
                "precision mediump float;                              \n"
                "in vec2 v_texCoord;                                   \n"
                "layout(location = 0) out vec4 outColor;               \n"
                "uniform sampler2D s_texture;                          \n"
                "void main()                                           \n"
                "{                                                     \n"
                "  outColor = texture( s_texture, v_texCoord * 10.0 ); \n"
                "}                                                     \n";

        frag = create_shader(frag_shader_text, GL_FRAGMENT_SHADER);
        vert = create_shader(vert_shader_text, GL_VERTEX_SHADER);

        program = glCreateProgram();
        glAttachShader(program, frag);
        glAttachShader(program, vert);
        glLinkProgram(program);

        int status;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            char log[1000];
            GLsizei len;
            glGetProgramInfoLog(program, 1000, &len, log);
            fprintf(stderr, "Error: linking:\n%*s\n", len, log);
            exit(1);
        }

        glUseProgram(program);

        auto s_texture = glGetUniformLocation(program, "s_texture");
        glUniform1i(s_texture, 0);
    }

    // vertices
    {
        static const GLfloat vVertices[] = {
                -0.9f, 0.9f,  0.0f,  // Position 0
                0.0f,  0.0f,         // TexCoord 0
                -0.9f, -0.9f, 0.0f,  // Position 1
                0.0f,  1.0f,         // TexCoord 1
                0.9f,  -0.9f, 0.0f,  // Position 2
                1.0f,  1.0f,         // TexCoord 2
                0.9f,  0.9f,  0.0f,  // Position 3
                1.0f,  0.0f          // TexCoord 3
        };

        // Load the vertex position
        // index
        // size: the number of components per generic vertext attribute.
        // type
        // normalized
        // stride: specified the byte offset between cosecutive gnertic vertext
        //         attributes.
        // pointer: specifies a offset of the first component of the first generic
        //          vertex attributes.

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                              vVertices);
        // Load the texture coordinate
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                              &vVertices[3]);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }

    while(state.running) {
        wl_display_dispatch_pending(state.display);
        glClearColor(0.0/255, 79.0/255, 158.0/255, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        GLushort indices[] = {1 ,2, 0, 2, 3, 0};
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

        eglSwapBuffers(state.egl_display, state.egl_surface);
        fflush(stdout);
    }

    glDeleteTextures(1, &texture);
    glDeleteProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    eglDestroySurface(state.egl_display, state.egl_surface);
    eglDestroyContext(state.egl_display, state.egl_context);
    wl_egl_window_destroy(state.egl_window);
    xdg_toplevel_destroy(state.xdg_toplevel);
    xdg_surface_destroy(state.xdg_surface);
    wl_surface_destroy(state.surface);
    wl_display_disconnect(state.display);

    return 0;
}