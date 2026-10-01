// C0/G3 experiment, deliberately outside the installed/public shader ABI.
// Proves the proposed vertex-W, output-coordinate and opaque-depth contracts
// on the actual renderer context. Does not claim workspace/source integration.
#include "render_fixture.h"
#include "render/fx_renderer/scene_resources.h"

static GLuint program_create(const char *vertex, const char *fragment) {
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vertex);
	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fragment);
	if (!vert || !frag) {
		glDeleteShader(vert);
		glDeleteShader(frag);
		return 0;
	}
	GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glBindAttribLocation(program, 0, "mesh_uv");
	glLinkProgram(program);
	glDeleteShader(vert);
	glDeleteShader(frag);
	GLint linked;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		fprintf(stderr, "link: %s\n", log);
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

static const char vertex[] =
	"precision highp float;\n"
	"attribute vec2 mesh_uv; varying vec2 item_uv;\n"
	"uniform vec4 placement; uniform float perspective;\n"
	"void main() { item_uv = mesh_uv; float w = 1.0 + perspective * mesh_uv.x;\n"
	"vec2 xy = vec2(placement.x + mesh_uv.x * placement.y, mesh_uv.y);\n"
	"gl_Position = vec4((xy * 2.0 - 1.0) * w, placement.z * w, w); }\n";

static const char fragment[] =
	"precision mediump float; varying vec2 item_uv;\n"
	"uniform sampler2D item; uniform vec2 output_size; uniform float coordinates;\n"
	"void main() { vec2 output_uv = gl_FragCoord.xy / output_size;\n"
	"vec4 authored = mix(texture2D(item, item_uv), vec4(item_uv.x, output_uv.x, 0.0, 0.0), coordinates);\n"
	"gl_FragColor = vec4(authored.rgb, 1.0); }\n";

static void draw(GLuint program, const struct fx_scene_mesh *mesh,
		float x, float width, float z, float perspective, const uint8_t color[4]) {
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, color);
	glUniform4f(glGetUniformLocation(program, "placement"), x, width, z, 0);
	glUniform1f(glGetUniformLocation(program, "perspective"), perspective);
	glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_SHORT, mesh->indices);
}

static bool experiment(void) {
	GLint texture_limit, vertex_limit, fragment_limit, varying_limit;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vertex_limit);
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fragment_limit);
	glGetIntegerv(GL_MAX_VARYING_VECTORS, &varying_limit);
	fprintf(stderr, "G3 context: %s; texture=%d vertex_vectors=%d fragment_vectors=%d varying_vectors=%d\n",
		glGetString(GL_RENDERER), texture_limit, vertex_limit, fragment_limit, varying_limit);
	GLuint program = program_create(vertex, fragment);
	if (!check(program != 0, "proposed stages link")) {
		return false;
	}
	struct fx_scene_mesh mesh = {0};
	if (!fx_scene_mesh_create(&mesh, 1, 1, 64)) {
		glDeleteProgram(program);
		return false;
	}
	GLuint target, item, fbo, depth;
	glGenTextures(1, &target);
	glBindTexture(GL_TEXTURE_2D, target);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 128, 64);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	bool ok = check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
		"RGBA8 target with depth attachment");
	glViewport(0, 0, 128, 64);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glClearDepthf(1);
	glUseProgram(program);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, mesh.uv);
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &item);
	glBindTexture(GL_TEXTURE_2D, item);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glUniform1i(glGetUniformLocation(program, "item"), 0);
	glUniform2f(glGetUniformLocation(program, "output_size"), 128, 64);
	static const unsigned counts[] = {1, 2, 3, 4, 5, 8, 64};
	for (unsigned n = 0; n < sizeof(counts) / sizeof(counts[0]); n++) {
		unsigned count = counts[n];
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		for (unsigned i = 0; i < count; i++) {
			uint8_t color[] = {i * 3 + 16, 255 - i * 3, i + 32, 0};
			draw(program, &mesh, (float)i / count, 1.0f / count, 0, 0, color);
		}
		for (unsigned i = 0; i < count; i++) {
			uint8_t pixel[4];
			glReadPixels((i + 0.5) * 128 / count, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
			ok &= check(pixel[0] == i * 3 + 16 && pixel[1] == 255 - i * 3 &&
				pixel[2] == i + 32 && pixel[3] == 255, "all scene identities drawn with forced opaque alpha");
		}
	}
	const uint8_t red[] = {255, 0, 0, 0}, blue[] = {0, 0, 255, 0};
	for (unsigned reverse = 0; reverse < 2; reverse++) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		draw(program, &mesh, 0, 1, reverse ? 0.5f : -0.5f, 0, reverse ? blue : red);
		draw(program, &mesh, 0, 1, reverse ? -0.5f : 0.5f, 0, reverse ? red : blue);
		uint8_t pixel[4];
		glReadPixels(64, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		ok &= check(pixel[0] == 255 && pixel[2] == 0, "near opaque face wins independently of submission order");
	}
	// A negative control makes sure this pixel assertion detects absent depth.
	glDisable(GL_DEPTH_TEST);
	draw(program, &mesh, 0, 1, 0.5f, 0, blue);
	uint8_t pixel[4];
	glReadPixels(64, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	ok &= check(pixel[2] == 255 && pixel[0] == 0, "depth negative control changes the pixel");
	glEnable(GL_DEPTH_TEST);
	glUniform1f(glGetUniformLocation(program, "coordinates"), 1);
	for (unsigned perspective = 0; perspective < 2; perspective++) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		draw(program, &mesh, 0, 1, 0, perspective, red);
		glReadPixels(64, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		double output_u = 64.5 / 128.0;
		double item_u = perspective ? output_u / (2 - output_u) : output_u;
		ok &= check(fabs(pixel[0] - item_u * 255) < 2 && fabs(pixel[1] - output_u * 255) < 2,
			"item UV is perspective-correct and output UV derives from raster position");
	}
	ok &= check(glGetError() == GL_NO_ERROR, "no GL errors during projection experiment");
	glDisableVertexAttribArray(0);
	glDisable(GL_DEPTH_TEST);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDeleteRenderbuffers(1, &depth);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &target);
	glDeleteTextures(1, &item);
	glDeleteProgram(program);
	fx_scene_mesh_finish(&mesh);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	struct wlr_egl_context previous;
	struct fx_renderer *renderer = fx_get_renderer(fixture.renderer);
	bool ok = wlr_egl_make_current(renderer->egl, &previous);
	if (ok) {
		ok = experiment();
		wlr_egl_restore_context(&previous);
	}
	// An ordinary pass after teardown detects leaked depth/culling/program state.
	struct wlr_buffer *buffer = create_output_buffer(&fixture,
		DRM_FORMAT_ARGB8888, TEST_WIDTH, TEST_HEIGHT);
	struct wlr_render_pass *pass = buffer ?
		wlr_renderer_begin_buffer_pass(fixture.renderer, buffer, NULL) : NULL;
	if (pass) {
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
			.box = {.width = TEST_WIDTH, .height = TEST_HEIGHT},
			.color = {.g = 1, .a = 1}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		});
		ok &= check(wlr_render_pass_submit(pass), "ordinary pass after projection teardown");
		uint8_t pixel[4];
		ok &= fixture_read_pixel(&fixture, buffer, 8, 8, pixel) &&
			check(pixel[1] > 250 && pixel[0] < 5 && pixel[2] < 5,
				"projection teardown preserves subsequent ordinary rendering");
	} else {
		ok = false;
	}
	if (buffer) {
		wlr_buffer_drop(buffer);
	}
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
