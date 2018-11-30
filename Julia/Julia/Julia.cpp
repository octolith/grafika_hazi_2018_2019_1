//=============================================================================================
// Julia fractals on the GPU
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
int majorVersion = 3, minorVersion = 3;

//---------------------------
class Shader {
	//--------------------------
	void getErrorInfo(unsigned int handle) {
		int logLen, written;
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logLen);
		if (logLen > 0) {
			char * log = new char[logLen];
			glGetShaderInfoLog(handle, logLen, &written, log);
			printf("Shader log:\n%s", log);
			delete log;
		}
	}
	void checkShader(unsigned int shader, char * message) { 	// check if shader could be compiled
		int OK;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &OK);
		if (!OK) { printf("%s!\n", message); getErrorInfo(shader); getchar(); }
	}
	void checkLinking(unsigned int program) { 	// check if shader could be linked
		int OK;
		glGetProgramiv(program, GL_LINK_STATUS, &OK);
		if (!OK) { printf("Failed to link shader program!\n"); getErrorInfo(program); getchar(); }
	}
public:
	unsigned int shaderProgram, vertexShader, geometryShader, fragmentShader;

	Shader() {
		// Create vertex shader from string
		vertexShader = glCreateShader(GL_VERTEX_SHADER);
		if (!vertexShader) {
			printf("Error in vertex shader creation\n");
			exit(1);
		}

		// Create fragment shader from string
		fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		if (!fragmentShader) {
			printf("Error in fragment shader creation\n");
			exit(1);
		}

		geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
		if (!geometryShader) {
			printf("Error in geometry shader creation\n");
			exit(1);
		}

		shaderProgram = glCreateProgram();
		if (!shaderProgram) {
			printf("Error in shader program creation\n");
			exit(1);
		}
	}

	void Attach(const char * vertexSource, const char * geometrySource, const char * fragmentSource) {
		if (vertexSource) {
			glShaderSource(vertexShader, 1, &vertexSource, NULL);
			glCompileShader(vertexShader);
			checkShader(vertexShader, "Vertex shader error");
			glAttachShader(shaderProgram, vertexShader);
		}


		// Create geometry shader from string
		if (geometrySource) {
			glShaderSource(geometryShader, 1, &geometrySource, NULL);
			glCompileShader(geometryShader);
			checkShader(geometryShader, "Geometry shader error");
			glAttachShader(shaderProgram, geometryShader);
		}

		if (fragmentSource) {
			glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
			glCompileShader(fragmentShader);
			checkShader(fragmentShader, "Fragment shader error");
			glAttachShader(shaderProgram, fragmentShader);
			// Connect the fragmentColor to the frame buffer memory
			glBindFragDataLocation(shaderProgram, 0, "fragmentColor");	// fragmentColor goes to the frame buffer memory

			// program packaging
			glLinkProgram(shaderProgram);
			checkLinking(shaderProgram);
		}
	}

	~Shader() { glDeleteProgram(shaderProgram); }
};

//---------------------------
class IterationShader : public Shader {
	//---------------------------
		// vertex shader in GLSL
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		uniform vec2 cameraCenter;
		uniform vec2 cameraSize;

		layout(location = 0) in vec2 cVertex;	// Attrib Array 0
		out vec2 z0;						    // output attribute

		void main() {
			gl_Position = vec4(cVertex, 0, 1);
			z0 = cVertex * cameraSize/2 + cameraCenter;		
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		uniform vec2 c;
		uniform int function;

		in vec2 z0;					// variable input
		out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

		vec2 expComplex(vec2 z) {
			return vec2(cos(z.y), sin(z.y)) * exp(z.x);
		}

		vec2 cosComplex(vec2 z) {
			vec2 zi = vec2(-z.y, z.x);
			return (expComplex(zi) + expComplex(-zi))/2;
		}

		void main() { 
			vec2 z = z0;
			for(int i = 0; i < 1000; i++) z = $;
			fragmentColor = (dot(z, z) < 100) ? vec4(0, 0, 0, 1) : vec4(1, 1, 1, 1); 
		}
	)";

public:
	IterationShader() { Attach(vertexSource, NULL, NULL); }

	void EditFragment(char * instruction) {
		char newFragmentSource[8000];
		char *sw = &newFragmentSource[0];
		const char *sr = fragmentSource;


		while (*sr != '\0') {
			if (*sr == '$')
				while (*instruction) *sw++ = *instruction++;
			else *sw++ = *sr;
			sr++;
		}
		*sw = '\0';
		Attach(NULL, NULL, newFragmentSource);
	}
};


//---------------------------
class InverseIterationShader : public Shader {
	//---------------------------
		// vertex shader in GLSL
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		layout(location = 0) in vec2 zRoot;	// Attrib Array 0

		void main() { gl_Position = vec4(zRoot, 0, 1);	}
	)";

	// geometry shader in GLSL
	const char * geometrySource = R"(
		#version 330
		precision highp float;

		uniform vec2 cameraCenter;
		uniform vec2 cameraSize;
		uniform vec2 c;

		#define nPoints 63
		
		layout(points) in;
		layout(points, max_vertices = nPoints) out;

		vec2 sqrtComplex(vec2 z) {
			float r = length(z);
			float phi = atan(z.y, z.x);
			return vec2(cos(phi / 2), sin(phi / 2)) * sqrt(r);
		}

		void main() {
			vec2 zs[nPoints];
			zs[0] = gl_in[0].gl_Position.xy;
			gl_Position = vec4((zs[0] - cameraCenter) / (cameraSize/2), 0, 1);
			EmitVertex();

			for(int i = 0; i < nPoints/2; i++) {
				vec2 z = sqrtComplex(zs[i] - c);
				for(int j = 1; j <= 2; j++) {
					zs[2 * i + j] = z;
					gl_Position = vec4((z - cameraCenter) / (cameraSize/2), 0, 1);	
					EmitVertex();
					z = -z;
				}	
			}
			EndPrimitive();
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation
		void main() { fragmentColor = vec4(0, 0, 0, 0); }
	)";

public:
	InverseIterationShader() {
		Attach(vertexSource, geometrySource, fragmentSource);
	}
};

// 2D point in Cartesian coordinates
struct vec2 {
	float x, y;

	vec2(float _x = 0, float _y = 0) { x = _x; y = _y; }
	vec2 operator-(vec2& right) {
		vec2 res(x - right.x, y - right.y);
		return res;
	}
	vec2 operator+(vec2& right) {
		vec2 res(x + right.x, y + right.y);
		return res;
	}
	vec2 operator*(float s) {
		vec2 res(x * s, y * s);
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

vec2 sqrt(vec2 z) {
	float r = z.Length();
	float phi = atan2(z.y, z.x);
	return vec2(cosf(phi / 2), sinf(phi / 2)) * sqrtf(r);
}

// 2D camera
struct Camera {
	vec2 wCenter;	// center in world coordinates
	vec2 wSize;	// width and height in world coordinates

	Camera() {
		wSize = vec2(4, 4);
		wCenter = vec2(0, 0);
	}
	void SetUniform(unsigned int shaderProg) {
		wSize.SetUniform(shaderProg, "cameraSize");
		wCenter.SetUniform(shaderProg, "cameraCenter");
	}
};

// 2D camera
Camera camera;

// handle of the shader program
InverseIterationShader * inverseIterationShader;
IterationShader * iterationShader;

vec2 c(0, 0);

class Seed {
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

	void Draw() {
		glUseProgram(inverseIterationShader->shaderProgram);
		c.SetUniform(inverseIterationShader->shaderProgram, "c");
		camera.SetUniform(inverseIterationShader->shaderProgram);

		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source
		glBindBuffer(GL_ARRAY_BUFFER, vbo); // make it active, it is an array

		vec2 z = vec2(0.5, 0) + sqrt(vec2(0.25 - c.x, -c.y));
		z = -sqrt(z - c);

		const int nSeedsPerPacket = 10000;
		const int nPackets = 100;

		for (int p = 0; p < nPackets; p++) {
			vec2 vertices[nSeedsPerPacket];
			for (int i = 0; i < nSeedsPerPacket; i++) {
				z = sqrt(z - c) * (rand() & 1 ? 1 : -1);
				vertices[i] = -z;
			}
			glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
			glDrawArrays(GL_POINTS, 0, nSeedsPerPacket);
		}
	}
};


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

	void Draw() {
		glUseProgram(iterationShader->shaderProgram);
		c.SetUniform(iterationShader->shaderProgram, "c");
		camera.SetUniform(iterationShader->shaderProgram);
		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// draw two triangles forming a quad
	}
};

// The virtual world: collection of objects
Seed seeds;
FullScreenQuad quad;
bool inverseIteration = false;

// popup menu event handler
void processMenuEvents(int option) {
	char buffer[256], *instruction;
	inverseIteration = false;

	switch (option) {
	case 0: instruction = "vec2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + c";
		iterationShader->EditFragment(instruction);
		break;
	case 1: instruction = "expComplex(z) + c";
		iterationShader->EditFragment(instruction);
		break;
	case 2: instruction = "cosComplex(z + c)";
		iterationShader->EditFragment(instruction);
		break;
	case 3: printf("\nz = ");
		instruction = &buffer[0];
		while ((*instruction++ = getchar()) != '\n');
		*(instruction - 1) = '\0';
		iterationShader->EditFragment(&buffer[0]);
		break;
	case 4: inverseIteration = true;
	}
	glutPostRedisplay();
}

// Initialization, create an OpenGL context
void onInitialization() {
	// create the menu and tell glut that "processMenuEvents" will handle the events: Do not use it in your homework, because it is prohitibed by the portal
	int menu = glutCreateMenu(processMenuEvents);
	//add entries to our menu
	glutAddMenuEntry("Filled: z^2 + c", 0);
	glutAddMenuEntry("Filled: exp(z) + c", 1);
	glutAddMenuEntry("Filled: cos(z + c)", 2);
	glutAddMenuEntry("Filled: user defined", 3);
	glutAddMenuEntry("Inverse Iteration: z^2 + c", 4);
	// attach the menu to the right button
	glutAttachMenu(GLUT_RIGHT_BUTTON);

	glViewport(0, 0, windowWidth, windowHeight);

	quad.Create();
	iterationShader = new IterationShader();
	processMenuEvents(0);

	seeds.Create();
	inverseIterationShader = new InverseIterationShader();
}


// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(1, 1, 1, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	if (inverseIteration) seeds.Draw();
	else                  quad.Draw();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	if (key == ' ') {
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
	c = vec2(cX, cY);
	glutPostRedisplay();
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON) {
		if (state == GLUT_DOWN) {
			float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
			float cY = 1.0f - 2.0f * pY / windowHeight;
			c = vec2(cX, cY);
		}
	}
	glutPostRedisplay();
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
	return 1;
}