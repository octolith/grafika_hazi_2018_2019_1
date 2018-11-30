//=============================================================================================
// Image Viewer
//=============================================================================================
#include "framework.h"

// vertex shader in GLSL
const char * vertexSource = R"(
	#version 330
	precision highp float;

	layout(location = 0) in vec2 vertexPosition;	// Attrib Array 0

	out vec2 texCoord;								// output attribute

	void main() {
		texCoord = (vertexPosition + vec2(1, 1)) / 2;						// from clipping to texture space
		gl_Position = vec4(vertexPosition.x, vertexPosition.y, 0, 1); 		// already in clipping space
	}
)";

// fragment shaders in GLSL
const char * fragmentSources[] = {
	// Lens effect fragment shader
		R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		const float maxRadius2 = 0.03f; 
		float d2 = dot(texCoord - texCursor, texCoord - texCursor) / maxRadius2;
		if (d2 > 1) d2 = 1;
		vec2 transfTexCoord = (texCoord - texCursor) * d2 + texCursor;
		fragmentColor = texture(textureUnit, transfTexCoord);
	}
)",
// Blackhole effect fragment shader
R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		const float r0 = 0.09f, ds = 0.001;
		vec3 p = vec3(texCoord, 0), dir = vec3(0,0,1), blackhole = vec3(texCursor, 0.5);
		float r2 = dot(blackhole - p, blackhole - p);
		while (p.z < 1 && r2 > r0 * r0) {
			p += dir * ds;
			r2 = dot(blackhole - p, blackhole - p);
			vec3 gDir = (blackhole - p)/sqrt(r2); // gravity direction
			dir = normalize(dir * ds + gDir * r0 / r2 / 4 * ds * ds);
		} 
		if (p.z >= 1) fragmentColor = texture(textureUnit,vec2(p.x,p.y));
		else          fragmentColor = vec4(0, 0, 0, 1);
	}
)",
// Gaussian Blur fragment shader
R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		const int filterSize = 9;
		const float ds = 0.003f;
		float sigma2 = (dot(texCoord-texCursor, texCoord-texCursor)/5 + 0.001f) * ds;

		fragmentColor = vec4(0, 0, 0, 0);
		float totalWeight = 0f;
		for(int X = -filterSize; X <= filterSize; X++) {
			for(int Y = -filterSize; Y <= filterSize; Y++) {
				vec2 offset = vec2(X * ds, Y * ds);
				float weight = exp(-dot(offset, offset) / 2 / sigma2 );
				fragmentColor += texture(textureUnit, texCoord + offset) * weight;
				totalWeight += weight;
			}
		}
		fragmentColor /= totalWeight;
	}
)",
// Scale space edge detection
R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	float NTSC(vec3 color) { return dot(color, vec3(0.33f, 0.71f, 0.08f)); }

	void main() {
		const int filterSize = 9;
		const float ds = 0.003f;
		float sigma2 = (dot(texCoord-texCursor, texCoord-texCursor)/5 + 0.001f) * ds;	// control scale by the distance from cursor

		fragmentColor = vec4(0, 0, 0, 0);
		float a = 1.0f / sigma2;
		float ad = -1.0f / sigma2 / sigma2;

		vec2 gradient = vec2(0, 0);
		float totalWeight = 0;

		for(int X = -filterSize; X <= filterSize; X++) {
			for(int Y = -filterSize; Y <= filterSize; Y++) {
				vec2 offset = vec2(X * ds, Y * ds);
				float weight = a * exp(-dot(offset, offset) / 2 / sigma2 );
				totalWeight += weight;
				vec2 gradientWeight = -weight / sigma2 * offset;
				gradient += NTSC(texture(textureUnit, texCoord + offset).rgb) * gradientWeight;
			}
		}
		float luminance = length(gradient) / totalWeight * 0.1;
		fragmentColor = vec4(luminance, luminance, luminance, 1);
	}
)",
// Swirl fragment shader
R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;

	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		float angle = 8 * exp( -15 * length(texCoord - texCursor) );
		mat2 rotationMatrix = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
		vec2 transformedTexCoord = (texCoord - texCursor) * rotationMatrix + texCursor;
		fragmentColor = texture(textureUnit, transformedTexCoord);
	}
)",
// Wave fragment shader
R"(
	#version 330
	precision highp float;

	uniform sampler2D textureUnit;
	uniform vec2 texCursor;
	uniform float waveTime;

	const float c = 0.1;	 // speed of the weight;
	const float n = 1.3; // index of refraction of water
	const float alphaMax = 0.1;
	const float waveWidth = 0.03;
	const float waterDepth = 1.0;
	const float PI = 3.141582;
	in vec2 texCoord;			// variable input: interpolated texture coordinates
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		float distance = length(texCoord - texCursor);
		vec2 changeDir = (texCoord - texCursor) / distance;
		float waveFrontDistance = c * waveTime; 
		if (abs(distance - waveFrontDistance) < waveWidth) {
			float alphaIn = alphaMax  / waveFrontDistance * sin((waveFrontDistance - distance)/waveWidth * PI);
			float alphaRefract = asin(sin(alphaIn) / n);
			vec2 transformedTexCoord = texCoord + changeDir * tan(alphaIn - alphaRefract) * waterDepth;
			fragmentColor = texture(textureUnit, transformedTexCoord);
		} else {
			fragmentColor = texture(textureUnit, texCoord);
		}
	}
)"
};

std::vector<vec3> CheckerBoard(int& width, int& height) {	// Procedural checker board texture on CPU 
	width = height = 128;
	std::vector<vec3> image(width * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			image[y * width + x] = (((x / 16) % 2) ^ ((y / 16) % 2)) ? vec3(1, 1, 0) : vec3(0, 0, 1);
		}
	}
	return image;
}

std::vector<vec3> ReadBMP(char * pathname, int& width, int& height) {	// read image as BMP files 
	FILE * file = fopen(pathname, "r");
	if (!file) {
		printf("%s does not exist\n", pathname);
		return CheckerBoard(width, height);	// replace it with a checker board
	}
	unsigned short bitmapFileHeader[27];					// bitmap header
	fread(&bitmapFileHeader, 27, 2, file);
	if (bitmapFileHeader[0] != 0x4D42) {   // magic number
		printf("Not bmp file\n");
		return CheckerBoard(width, height);
	}
	if (bitmapFileHeader[14] != 24) {
		printf("Only true color bmp files are supported\n");
		return CheckerBoard(width, height);
	}
	width = bitmapFileHeader[9];
	height = bitmapFileHeader[11];
	unsigned int size = (unsigned long)bitmapFileHeader[17] + (unsigned long)bitmapFileHeader[18] * 65536;
	fseek(file, 54, SEEK_SET);

	std::vector<byte> byteImage(size);
	fread(&byteImage[0], 1, size, file); 	// read the pixels
	fclose(file);

	std::vector<vec3> image(width * height);

	// Swap R and B since in BMP, the order is BGR
	int i = 0;
	for (int imageIdx = 0; imageIdx < size; imageIdx += 3) {
		image[i++] = vec3(byteImage[imageIdx + 2] / 256.0f, byteImage[imageIdx + 1] / 256.0f, byteImage[imageIdx] / 256.0f);
	}
	return image;
}

// handle of the shader program
const int nEffects = 6;
enum Effect { LENS, BLACKHOLE, GAUSSIAN, EDGE, SPIRAL, WAVE };
Effect effect = LENS;
GPUProgram shaderPrograms[nEffects];

vec2 texCursorPosition(0, 0);		// cursor position in texture space
float cursorPressTime = 0;

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

		int width = 128, height = 128;
		std::vector<vec3> image = ReadBMP("C:/3dprogramok/GrafikaHazi/Programs/ImageViewer/bin/image.bmp", width, height);

		// Create objects by setting up their vertex data on the GPU
		glGenTextures(1, &textureId);  				// id generation
		glBindTexture(GL_TEXTURE_2D, textureId);    // binding

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGB, GL_FLOAT, &image[0]); // To GPU
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // sampling
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	void Draw() {
		glBindVertexArray(vao);	// make the vao and its vbos active playing the role of the data source

		int location = glGetUniformLocation(shaderPrograms[effect].getId(), "texCursor");
		if (location >= 0) glUniform2f(location, texCursorPosition.x, texCursorPosition.y); // set uniform variable MVP to the MVPTransform
		else printf("texCursor cannot be set\n");

		location = glGetUniformLocation(shaderPrograms[effect].getId(), "textureUnit");
		if (location >= 0) {
			glUniform1i(location, 0);		// texture sampling unit is TEXTURE0
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, textureId);	// connect the texture to the sampler
		}
		if (effect == WAVE) {
			int location = glGetUniformLocation(shaderPrograms[effect].getId(), "waveTime");
			float waveTime = (glutGet(GLUT_ELAPSED_TIME) - cursorPressTime) / 1000.0f;
			if (location >= 0) glUniform1f(location, waveTime); // set uniform variable MVP to the MVPTransform
			else printf("waveTime cannot be set\n");
		}
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// draw two triangles forming a quad
	}
};

// popup menu event handler
void processMenuEvents(int option) {
	effect = (Effect)option;
	shaderPrograms[effect].Use();
}

// The virtual world: a single full screen quad
TexturedQuad quad;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);

	// create the menu and tell glut that "processMenuEvents" will handle the events: Do not use it in your homework, because it is prohitibed by the portal
	int menu = glutCreateMenu(processMenuEvents);
	//add entries to our menu
	glutAddMenuEntry("Lens effect", LENS);
	glutAddMenuEntry("Black hole ", BLACKHOLE);
	glutAddMenuEntry("Gaussian blur", GAUSSIAN);
	glutAddMenuEntry("Scale Space Edges", EDGE);
	glutAddMenuEntry("Swirl", SPIRAL);
	glutAddMenuEntry("Wave", WAVE);
	// attach the menu to the right button
	glutAttachMenu(GLUT_RIGHT_BUTTON);

	// create the full screen quad
	quad.Create();

	// create program for the GPU
	for (int eff = 0; eff < nEffects; eff++) {
		shaderPrograms[eff].Create(vertexSource, fragmentSources[eff], "fragmentColor");
	}
	shaderPrograms[effect].Use();

	printf("\nUsage: \n");
	printf("Mouse Left Button: Start or move effect here\n");
	printf("Mouse Right Button: Pop-up menu to select effect\n");
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
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {

}

bool mouseLeftPressed = false;

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
	if (mouseLeftPressed) {
		texCursorPosition.x = (float)pX / windowWidth;	// flip y axis
		texCursorPosition.y = 1.0f - (float)pY / windowHeight;
	}
	glutPostRedisplay();     // redraw
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON) {
		if (state == GLUT_DOWN) {
			mouseLeftPressed = true;
			cursorPressTime = glutGet(GLUT_ELAPSED_TIME);
		}
		else mouseLeftPressed = false;
	}
	onMouseMotion(pX, pY);
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
	glutPostRedisplay();     // redraw
}
