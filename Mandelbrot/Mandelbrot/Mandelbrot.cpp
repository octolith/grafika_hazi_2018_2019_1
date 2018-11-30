//=============================================================================================
// Mandelbrot set on the GPU
//=============================================================================================
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <vector>

#if defined(__APPLE__)
#include <GLUT/GLUT.h>
#include <OpenGL/gl3.h>
#include <OpenGL/glu.h>
#else
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#include <windows.h>
#endif
#include <GL/glew.h>		// must be downloaded 
#include <GL/freeglut.h>	// must be downloaded unless you have an Apple
#endif

const unsigned int windowWidth = 600, windowHeight = 600;

// OpenGL major and minor versions
int majorVersion = 4, minorVersion = 4;

void getErrorInfo(unsigned int handle) {
	int logLen;
	glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logLen);
	if (logLen > 0) {
		char * log = new char[logLen];
		int written;
		glGetShaderInfoLog(handle, logLen, &written, log);
		printf("Shader log:\n%s", log);
		delete log;
	}
}

// check if shader could be compiled
void checkShader(unsigned int shader, char * message) {
	int OK;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &OK);
	if (!OK) {
		printf("%s!\n", message);
		getErrorInfo(shader);
	}
}

// check if shader could be linked
void checkLinking(unsigned int program) {
	int OK;
	glGetProgramiv(program, GL_LINK_STATUS, &OK);
	if (!OK) {
		printf("Failed to link shader program!\n");
		getErrorInfo(program);
	}
}

// vertex shader in GLSL
const char * vertexSource = R"(
	#version 440
	precision highp float;

	uniform vec2 cameraCenter;
	uniform vec2 cameraSize;

	layout(location = 0) in vec2 cVertex;	// Attrib Array 0
	out vec2 c;								// output attribute

	void main() {
		gl_Position = vec4(cVertex, 0, 1);
		c = cVertex * (cameraSize/2) + cameraCenter;		
	}
)";

// fragment shader in GLSL
const char * fragmentSource = R"(
	#version 440
	precision highp float;

	uniform int fractalDraw;
	uniform int nIteration;

	in vec2 c;			
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

   float Mandelbrot(vec2 c) {
		vec2 z = c;
		int i;
		for(i = nIteration; i > 0; i--) {
			z = vec2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + c; // z_{n+1} = z_{n}^2 + c
			if (dot(z, z) > 4) break;
		}
		return i;
	}

	float HexaCone(float s1, float s2, float hue) {
		while (hue > 360)   hue -= 360;
		while (hue < 0)     hue += 360;
		if (hue < 60)   return (s1 + (s2 - s1) * hue / 60);
		if (hue < 180)  return (s2);
		if (hue < 240)  return (s1 + (s2 - s1) * (240 - hue) / 60);
		return (s1);
	}

	vec3 HLSToRGB(float H, float Lin, float Sin) {
		float L = min(0.5, Lin), S = min(1, Sin);
		float s2 = (L <= 0.5) ? L * (1 + S) : L * (1 - S) + S;
		float s1 = 2 * L - s2;
		if (S == 0)  return vec3(L, L, L);
		return vec3(HexaCone(s1, s2, H - 120), HexaCone(s1, s2, H), HexaCone(s1, s2, H + 120));
	}

	void main() {		
		if (fractalDraw == 1) {
			float result = Mandelbrot(c); 
			fragmentColor = (result > 0) ? vec4(HLSToRGB(result * 6, 0.5, 1), 1) : vec4(0, 0, 0, 1); 
		} else {
			fragmentColor = vec4(1, 1, 0, 1); 
		}
	}
)";

// 2D point in Cartesian coordinates
struct vec2 {
	float x, y;

	vec2(float _x = 0, float _y = 0) { x = _x; y = _y; }
	vec2 operator-(const vec2& right) {
		vec2 res(x - right.x, y - right.y);
		return res;
	}
	vec2 operator+(const vec2& right) {
		vec2 res(x + right.x, y + right.y);
		return res;
	}
	vec2 operator*(float s) {
		vec2 res(x * s, y * s);
		return res;
	}
	vec2 operator*(const vec2& right) {
		vec2 res(x * right.x, y * right.y);
		return res;
	}
	vec2 operator/(float s) {
		vec2 res(x / s, y / s);
		return res;
	}
	vec2 operator-() {
		return vec2(-x, -y);
	}

	float Length() { return sqrtf(x * x + y * y); }

	void SetUniform(unsigned shaderProg, char * name) {
		int location = glGetUniformLocation(shaderProg, name);
		if (location >= 0) glUniform2fv(location, 1, &x);
		else printf("uniform %s cannot be set\n", name);
	}
};
// 2D camera
vec2 wCameraCenter(0, 0), wCameraSize(5, 5);

// handle of the shader program
unsigned int shaderProgram;

class FullScreenQuad {
	unsigned int vao, vbo;	// vertex array object id and texture id
public:
	void Create() {
		glGenVertexArrays(1, &vao);	// create 1 vertex array object
		glBindVertexArray(vao);		// make it active
		glGenBuffers(1, &vbo);	// Generate 1 vertex buffer objects
		// vertex coordinates: vbo[0] -> Attrib Array 0 -> vertexPosition of the vertex shader
		glBindBuffer(GL_ARRAY_BUFFER, vbo); // make it active, it is an array
		float vertices[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);	   // copy to that part of the memory which will be modified 
		// Map Attribute Array 0 to the current bound vertex buffer (vbo[0])
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);     // stride and offset: it is tightly packed
	}

	void Draw(int nIter) {
		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source

		// set GPU uniform matrix variable MVP with the content of CPU variable MVPTransform
		wCameraSize.SetUniform(shaderProgram, "cameraSize");
		wCameraCenter.SetUniform(shaderProgram, "cameraCenter");

		int location = glGetUniformLocation(shaderProgram, "fractalDraw");
		if (location >= 0) glUniform1i(location, 1);
		else printf("uniform fractal cannot be set\n");

		location = glGetUniformLocation(shaderProgram, "nIteration");
		if (location >= 0) glUniform1i(location, nIter); // set uniform variable MVP to the MVPTransform
		else printf("uniform nIteration cannot be set\n");

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// draw two triangles forming a quad
	}
};

class Rect {
	unsigned int vao, vbo;	// vertex array object id and texture id
public:
	void Create() {
		glGenVertexArrays(1, &vao);	// create 1 vertex array object
		glBindVertexArray(vao);		// make it active
		glGenBuffers(1, &vbo);	// Generate 1 vertex buffer objects
		// vertex coordinates: vbo[0] -> Attrib Array 0 -> vertexPosition of the vertex shader
		glBindBuffer(GL_ARRAY_BUFFER, vbo); // make it active, it is an array
		// Map Attribute Array 0 to the current bound vertex buffer (vbo[0])
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);     // stride and offset: it is tightly packed
	}
	void Set(vec2 c1, vec2 c2) {
		glBindBuffer(GL_ARRAY_BUFFER, vbo); // make it active, it is an array
		float vertices[] = { c1.x, c1.y,   c2.x, c1.y,   c2.x, c2.y,  c1.x, c2.y };
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);	   // copy to that part of the memory which will be modified 
	}

	void Draw() {
		int location = glGetUniformLocation(shaderProgram, "fractalDraw");
		if (location >= 0) glUniform1i(location, 0); // set uniform variable MVP to the MVPTransform
		else printf("uniform whatToDraw cannot be set\n");

		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source
		glDrawArrays(GL_LINE_LOOP, 0, 4);	// draw two triangles forming a quad
	}
};


// The virtual world: collection of three objects
FullScreenQuad quad;
Rect rect;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);

	quad.Create();
	rect.Create();

	// Create vertex shader from string
	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	if (!vertexShader) {
		printf("Error in vertex shader creation\n");
		exit(1);
	}
	glShaderSource(vertexShader, 1, &vertexSource, NULL);
	glCompileShader(vertexShader);
	checkShader(vertexShader, "Vertex shader error");

	// Create fragment shader from string
	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fragmentShader) {
		printf("Error in fragment shader creation\n");
		exit(1);
	}
	glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
	glCompileShader(fragmentShader);
	checkShader(fragmentShader, "Fragment shader error");

	// Attach shaders to a single program
	shaderProgram = glCreateProgram();
	if (!shaderProgram) {
		printf("Error in shader program creation\n");
		exit(1);
	}
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);

	// Connect the fragmentColor to the frame buffer memory
	glBindFragDataLocation(shaderProgram, 0, "fragmentColor");	// fragmentColor goes to the frame buffer memory

	// program packaging
	glLinkProgram(shaderProgram);
	checkLinking(shaderProgram);
	// make this program run
	glUseProgram(shaderProgram);
}

void onExit() {
	glDeleteProgram(shaderProgram);
	printf("exit");
}

bool mouseLeftPressed = false;
vec2 corner1, corner2;

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 1, 1, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	quad.Draw(100);
	if (mouseLeftPressed) rect.Draw();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	if (key == ' ') {
		wCameraCenter = vec2(0, 0);
		wCameraSize = vec2(5, 5);
		glutPostRedisplay();
	}
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {

}

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
	float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
	float cY = 1.0f - 2.0f * pY / windowHeight;
	corner2 = vec2(cX, cY);
	rect.Set(corner1, corner2);
	glutPostRedisplay();     // redraw
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON) {
		if (state == GLUT_DOWN) {
			float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
			float cY = 1.0f - 2.0f * pY / windowHeight;
			corner1 = vec2(cX, cY);
			corner2 = corner1;
			rect.Set(corner1, corner2);
			mouseLeftPressed = true;
		}
		else {
			vec2 wCorner1 = corner1 * wCameraSize / 2 + wCameraCenter;
			vec2 wCorner2 = corner2 * wCameraSize / 2 + wCameraCenter;
			wCameraSize = vec2(fabs(wCorner1.x - wCorner2.x), fabs(wCorner1.y - wCorner2.y));
			wCameraCenter = (wCorner1 + wCorner2) * 0.5;
			mouseLeftPressed = false;
			glutPostRedisplay();     // redraw
		}
	}
}


// Idle event indicating that some time elapsed: do animation here
void onIdle() {
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Do not touch the code below this line

int main(int argc, char * argv[]) {
	glutInit(&argc, argv);
#if !defined(__APPLE__)
	glutInitContextVersion(majorVersion, minorVersion);
#endif
	glutInitWindowSize(windowWidth, windowHeight);				// Application window is initially of resolution 600x600
	glutInitWindowPosition(100, 100);							// Relative location of the application window
#if defined(__APPLE__)
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH | GLUT_3_3_CORE_PROFILE);  // 8 bit R,G,B,A + double buffer + depth buffer
#else
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
#endif
	glutCreateWindow(argv[0]);

#if !defined(__APPLE__)
	glewExperimental = true;	// magic
	glewInit();
#endif

	printf("GL Vendor    : %s\n", glGetString(GL_VENDOR));
	printf("GL Renderer  : %s\n", glGetString(GL_RENDERER));
	printf("GL Version (string)  : %s\n", glGetString(GL_VERSION));
	glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
	glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
	printf("GL Version (integer) : %d.%d\n", majorVersion, minorVersion);
	printf("GLSL Version : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	onInitialization();

	glutDisplayFunc(onDisplay);                // Register event handlers
	glutMouseFunc(onMouse);
	glutIdleFunc(onIdle);
	glutKeyboardFunc(onKeyboard);
	glutKeyboardUpFunc(onKeyboardUp);
	glutMotionFunc(onMouseMotion);

	glutMainLoop();
	onExit();
	return 1;
}