//=============================================================================================
// Root finder for f(x,y) = 0
//=============================================================================================
#include "framework.h"

//---------------------------
class GPGPUShader : public GPUProgram {
	//---------------------------
		// vertex shader in GLSL
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		uniform mat4 VPinv;
		layout(location = 0) in vec2 cVertex;	// Attrib Array 0
		out float x, y;							// output attribute

		void main() {
			vec4 wPos = vec4(cVertex.x, cVertex.y, 0, 1) * VPinv;
			x = wPos.x;
			y = wPos.y;						
			gl_Position = vec4(cVertex.x, cVertex.y, 0, 1); 		// already in clipping space
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSourceTemplate = R"(
		#version 330
		precision highp float;

		uniform float dx, dy;
		in float x, y;								
		out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

		float f1(float x, float y) {
			return $;
		}
		float f2(float x, float y) {
			return §;
		}

		void main() {	
			float scale = 2;	// scale pixels to improve visibility
			fragmentColor = vec4(0, 0, 0, 1);
			bvec4 res1 = bvec4(f1(x, y) > 0, f1(x+dx * scale, y) > 0, f1(x, y+dy * scale) > 0, f1(x+dx * scale, y+dy * scale) > 0);
			if (!(all(res1) || all(!res1))) fragmentColor += vec4(1, 0, 0, 0);  
			bvec4 res2 = bvec4(f2(x, y) > 0, f2(x+dx* scale, y) > 0, f2(x, y+dy* scale) > 0, f2(x+dx* scale, y+dy* scale) > 0);
			if (!(all(res2) || all(!res2))) fragmentColor += vec4(0, 1, 0, 0);  
		}
	)";

	// fragment shader in GLSL
	char fragmentSource[8000];

public:
	GPGPUShader() {
		char * inst1 = "x * x + y * y - 4";
		char * inst2 = "x * x - y * y - 1";
		printf("f1(x,y) = %s = 0\nf2(x,y) = %s = 0\n", inst1, inst2);
		EditFragment(inst1, inst2);	// default function
		// create program for the GPU
		Create(vertexSource, fragmentSource, "fragmentColor");
	}

	void AttachFragmentShader() {
		const char * fragmentSrc = &fragmentSource[0];
		glShaderSource(fragmentShader, 1, &fragmentSrc, NULL);
		glCompileShader(fragmentShader);
		if (checkShader(fragmentShader, "Fragment shader error")) glAttachShader(shaderProgramId, fragmentShader);
		// Connect the fragmentColor to the frame buffer memory
		glBindFragDataLocation(shaderProgramId, 0, "fragmentColor");	// fragmentColor goes to the frame buffer memory

		// program packaging
		glLinkProgram(shaderProgramId);
		if (checkLinking(shaderProgramId)) Use();
	}

	void EditFragment(char * instruction1, char * instruction2) {
		char *sw = &fragmentSource[0];
		const char *sr = fragmentSourceTemplate;
		while (*sr != '\0') {
			if (*sr == '$') while (*instruction1) *sw++ = *instruction1++;
			else if (*sr == '§') while (*instruction2) *sw++ = *instruction2++;
			else *sw++ = *sr;
			sr++;
		}
		*sw = '\0';
	}
};

// 2D camera
class Camera2D {
	vec2 wCenter; // center in world coordinates
	vec2 wSize;   // width and height in world coordinates

public:
	Camera2D() : wCenter(0, 0), wSize(20, 20) { }

	mat4 V() { return TranslateMatrix(-wCenter); }
	mat4 P() { return ScaleMatrix(vec2(2 / wSize.x, 2 / wSize.y)); }

	mat4 Vinv() { return TranslateMatrix(wCenter); }
	mat4 Pinv() { return ScaleMatrix(vec2(wSize.x / 2, wSize.y / 2)); }

	void Zoom(float s) { wSize = wSize * s; }
	void Pan(vec2 t) { wCenter = wCenter + t; }
	float dX() { return wSize.x / windowWidth; }
	float dY() { return wSize.y / windowHeight; }
};

Camera2D camera; // 2D camera
GPGPUShader * gpgpuShader;

class TexturedQuad {
	unsigned int vao, vbo, textureId;	// vertex array object id and texture id
	vec2 vertices[4];
public:
	TexturedQuad() {
		vertices[0] = vec2(-1, -1);
		vertices[1] = vec2(1, -1);
		vertices[2] = vec2(1, 1);
		vertices[3] = vec2(-1, 1);
	}
	void Create() {
		glGenVertexArrays(1, &vao);	// create 1 vertex array object
		glBindVertexArray(vao);		// make it active

		glGenBuffers(1, &vbo);	// Generate 1 vertex buffer objects
		// vertex coordinates: vbo[0] -> Attrib Array 0 -> vertexPosition of the vertex shader
		glBindBuffer(GL_ARRAY_BUFFER, vbo); // make it active, it is an array
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);	   // copy to that part of the memory which is not modified 
		// Map Attribute Array 0 to the current bound vertex buffer (vbo[0])
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);     // stride and offset: it is tightly packed
	}

	void Draw() {
		int location = glGetUniformLocation(gpgpuShader->getId(), "dx");
		if (location >= 0) glUniform1f(location, camera.dX());
		location = glGetUniformLocation(gpgpuShader->getId(), "dy");
		if (location >= 0) glUniform1f(location, camera.dY());
		mat4 VPinvTransform = camera.Pinv() * camera.Vinv();
		VPinvTransform.SetUniform(gpgpuShader->getId(), "VPinv");

		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// draw two triangles forming a quad
	}
};

// The virtual world: a single full screen quad
TexturedQuad quad;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);

	// create the full screen quad
	quad.Create();
	gpgpuShader = new GPGPUShader();

	printf("\nUsage: \n");
	printf("Space: Enter f(x, y) = \n");
	printf("Key 's': Camera pan -x\n");
	printf("Key 'd': Camera pan +x\n");
	printf("Key 'x': Camera pan -y\n");
	printf("Key 'e': Camera pan +y\n");
	printf("Key 'z': Camera zoom in\n");
	printf("Key 'Z': Camera zoom out\n");
	printf("Mouse click: Get point coordinates\n");
}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 0, 0, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	quad.Draw();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	char buffer1[256], buffer2[256], *instruction1, *instruction2;

	switch (key) {
	case 's': camera.Pan(vec2(-1, 0)); break;
	case 'd': camera.Pan(vec2(+1, 0)); break;
	case 'e': camera.Pan(vec2(0, 1)); break;
	case 'x': camera.Pan(vec2(0, -1)); break;
	case 'z': camera.Zoom(0.9f); break;
	case 'Z': camera.Zoom(1.1f); break;
	case ' ':
		printf("\nf1(x,y) = ");
		instruction1 = &buffer1[0];
		while ((*instruction1++ = getchar()) != '\n');
		*(instruction1 - 1) = '\0';
		printf("\nf2(x,y) = ");
		instruction2 = &buffer2[0];
		while ((*instruction2++ = getchar()) != '\n');
		*(instruction2 - 1) = '\0';
		gpgpuShader->EditFragment(&buffer1[0], &buffer2[0]);
		gpgpuShader->AttachFragmentShader();
	}
	glutPostRedisplay();
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {
}

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
	float cY = 1.0f - 2.0f * pY / windowHeight;
	vec4 wVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv();
	printf("\nx=%f, y=%f\n", wVertex.x, wVertex.y);
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
}
