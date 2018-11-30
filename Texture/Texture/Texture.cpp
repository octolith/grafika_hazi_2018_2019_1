//=============================================================================================
// Texture mapping: textures are generated on the CPU and on the GPU
//=============================================================================================
#include "framework.h"

// vertex shader in GLSL
const char * vertexSource = R"(
	#version 330
	precision highp float;

	uniform mat4 MVP;			// Model-View-Projection matrix in row-major format

	layout(location = 0) in vec2 vertexPosition;	// Attrib Array 0
	layout(location = 1) in vec2 vertexUV;			// Attrib Array 1

	out vec2 texCoord;								// output attribute

	void main() {
		texCoord = vertexUV;														// copy texture coordinates
		gl_Position = vec4(vertexPosition.x, vertexPosition.y, 0, 1) * MVP; 		// transform to clipping space
	}
)";

// fragment shader in GLSL
const char * fragmentSource = R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform int isGPUProcedural;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	int Mandelbrot(vec2 c) {
		vec2 z = c;
		for(int i = 10000; i > 0; i--) {
			z = vec2(z.x * z.x - z.y * z.y + c.x, 2 * z.x * z.y + c.y); // z_{n+1} = z_{n}^2 + c
			if (dot(z, z) > 4) return i;
		}
		return 0;
	}

	void main() {
		if (isGPUProcedural != 0) {
			int i = Mandelbrot(texCoord * 3 - vec2(2, 1.5)); 
			fragmentColor = vec4((i % 5)/5.0f, (i % 11) / 11.0f, (i % 31) / 31.0f, 1); 
		} else {
			fragmentColor = texture(textureUnit, texCoord);
		}
	}
)";


// 2D camera
class Camera2D {
	vec2 wCenter; // center in world coordinates
	vec2 wSize;   // width and height in world coordinates

public:
	Camera2D() : wCenter(0, 0), wSize(20, 20) { }

	mat4 V() { return TranslateMatrix(-wCenter); }

	mat4 P() { // projection matrix: 
		return ScaleMatrix(vec2(2 / wSize.x, 2 / wSize.y));
	}

	mat4 Vinv() { return TranslateMatrix(wCenter); }

	mat4 Pinv() { // inverse projection matrix
		return ScaleMatrix(vec2(wSize.x / 2, wSize.y / 2));
	}

	void Zoom(float s) { wSize = wSize * s; }
	void Pan(vec2 t) { wCenter = wCenter + t; }
};


// 2D camera
Camera2D camera;
GPUProgram gpuProgram; // vertex and fragment shaders
bool isGPUProcedural = false;

class TexturedQuad {
	unsigned int vao, vbo[2];
	vec2 vertices[4], uvs[4];
	Texture * pTexture;
public:
	TexturedQuad() {
		vertices[0] = vec2(-10, -10); uvs[0] = vec2(0, 0);
		vertices[1] = vec2(10, -10);  uvs[1] = vec2(1, 0);
		vertices[2] = vec2(10, 10);   uvs[2] = vec2(1, 1);
		vertices[3] = vec2(-10, 10);  uvs[3] = vec2(0, 1);
	}
	void Create() {
		glGenVertexArrays(1, &vao);	// create 1 vertex array object
		glBindVertexArray(vao);		// make it active

		glGenBuffers(2, vbo);	// Generate 1 vertex buffer objects

		// vertex coordinates: vbo[0] -> Attrib Array 0 -> vertexPosition of the vertex shader
		glBindBuffer(GL_ARRAY_BUFFER, vbo[0]); // make it active, it is an array
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);	   // copy to that part of the memory which will be modified 
		// Map Attribute Array 0 to the current bound vertex buffer (vbo[0])
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);     // stride and offset: it is tightly packed

		glBindBuffer(GL_ARRAY_BUFFER, vbo[1]); // make it active, it is an array
		glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);	   // copy to that part of the memory which is not modified 
		// Map Attribute Array 0 to the current bound vertex buffer (vbo[0])
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);     // stride and offset: it is tightly packed

		int width = 128, height = 128;
		std::vector<vec4> image(width * height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				float luminance = ((x / 16) % 2) ^ ((y / 16) % 2);
				image[y * width + x] = vec4(luminance, luminance, luminance, 1);
			}
		}

		pTexture = new Texture(width, height, image);
	}

	void MoveVertex(float cX, float cY) {
		glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);

		vec4 wCursor4 = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv();
		vec2 wCursor(wCursor4.x, wCursor4.y);

		int closestVertex = 0;
		float distMin = length(vertices[0] - wCursor);
		for (int i = 1; i < 4; i++) {
			float dist = length(vertices[i] - wCursor);
			if (dist < distMin) {
				distMin = dist;
				closestVertex = i;
			}
		}
		vertices[closestVertex] = wCursor;

		// copy data to the GPU
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);	   // copy to that part of the memory which is modified 
	}

	void Draw() {
		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source

		mat4 MVPTransform = camera.V() * camera.P();

		// set GPU uniform matrix variable MVP with the content of CPU variable MVPTransform
		MVPTransform.SetUniform(gpuProgram.getId(), "MVP");

		int location = glGetUniformLocation(gpuProgram.getId(), "isGPUProcedural");
		if (location >= 0) glUniform1i(location, isGPUProcedural); // set uniform variable MVP to the MVPTransform
		else printf("isGPUProcedural cannot be set\n");

		pTexture->SetUniform(gpuProgram.getId(), "textureUnit");

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// draw two triangles forming a quad
	}
};

// The virtual world: collection of three objects
TexturedQuad quad;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);

	quad.Create();

	// create program for the GPU
	gpuProgram.Create(vertexSource, fragmentSource, "fragmentColor");

	printf("\nUsage: \n");
	printf("Mouse Left Button: Pick and move vertex\n");
	printf("SPACE: Toggle between checkerboard (cpu) and Mandelbrot (gpu) textures\n");
}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 1, 1, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	quad.Draw();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	if (key == ' ') {
		isGPUProcedural = !isGPUProcedural;
		glutPostRedisplay();         // if d, invalidate display, i.e. redraw
	}
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {

}

bool mouseLeftPressed = false;
bool mouseRightPressed = false;

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
	if (mouseLeftPressed) {  // GLUT_LEFT_BUTTON / GLUT_RIGHT_BUTTON and GLUT_DOWN / GLUT_UP
		float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
		float cY = 1.0f - 2.0f * pY / windowHeight;
		quad.MoveVertex(cX, cY);
	}
	if (mouseRightPressed) {  // GLUT_LEFT_BUTTON / GLUT_RIGHT_BUTTON and GLUT_DOWN / GLUT_UP
	}
	glutPostRedisplay();     // redraw
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON) {
		if (state == GLUT_DOWN) mouseLeftPressed = true;
		else					mouseLeftPressed = false;
	}
	if (button == GLUT_RIGHT_BUTTON) {
		if (state == GLUT_DOWN) mouseRightPressed = true;
		else					mouseRightPressed = false;
	}
	onMouseMotion(pX, pY);
}


// Idle event indicating that some time elapsed: do animation here
void onIdle() {
}
