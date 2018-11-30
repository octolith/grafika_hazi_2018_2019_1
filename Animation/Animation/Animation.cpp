//=============================================================================================
// Primitive Man
//=============================================================================================
#include "framework.h"

//---------------------------
class PhongShader : public GPUProgram {
	//---------------------------
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		uniform mat4  MVP, M, Minv; // MVP, Model, Model-inverse
		uniform vec3  wLiDir;       // light source direction 
		uniform vec3  wEye;         // pos of eye

		layout(location = 0) in vec3  vtxPos;            // pos in modeling space
		layout(location = 1) in vec3  vtxNorm;      	 // normal in modeling space
		layout(location = 2) in vec2  vtxUV;

		out vec3 wNormal;		    // normal in world space
		out vec3 wView;             // view in world space
		out vec3 wLight;		    // light dir in world space

		void main() {
		   gl_Position = vec4(vtxPos, 1) * MVP; // to NDC
		   vec4 wPos = vec4(vtxPos, 1) * M;
		   wLight  = wLiDir;
		   wView   = wEye - wPos.xyz;
		   wNormal = (Minv * vec4(vtxNorm, 0)).xyz;
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		uniform vec3 kd, ks, ka; // diffuse, specular, ambient ref
		uniform vec3 La, Le;     // ambient and point sources
		uniform float shine;     // shininess for specular ref

		in  vec3 wNormal;       // interpolated world sp normal
		in  vec3 wView;         // interpolated world sp view
		in  vec3 wLight;        // interpolated world sp illum dir
		in vec2 texcoord;
		out vec4 fragmentColor; // output goes to frame buffer

		void main() {
			vec3 N = normalize(wNormal);
			vec3 V = normalize(wView); 
			vec3 L = normalize(wLight);
			vec3 H = normalize(L + V);
			float cost = max(dot(N,L), 0), cosd = max(dot(N,H), 0);
			vec3 color = ka * La + (kd * cost + ks * pow(cosd,shine)) * Le;
			fragmentColor = vec4(color, 1);
		}
	)";
public:
	PhongShader() { Create(vertexSource, fragmentSource, "fragmentColor"); }
};

PhongShader * gpuProgram; // vertex and fragment shaders

//---------------------------
struct Camera { // 3D camera
//---------------------------
	vec3 wEye, wLookat, wVup;
	float fov, asp, fp, bp;
public:
	Camera() {
		asp = 1;
		fov = 80.0f * (float)M_PI / 180.0f;
		fp = 0.1; bp = 100;
	}
	mat4 V() { // view matrix: translates the center to the origin
		vec3 w = normalize(wEye - wLookat);
		vec3 u = normalize(cross(wVup, w));
		vec3 v = cross(w, u);
		return TranslateMatrix(-wEye) * mat4(u.x, v.x, w.x, 0,
			u.y, v.y, w.y, 0,
			u.z, v.z, w.z, 0,
			0, 0, 0, 1);
	}
	mat4 P() { // projection matrix
		return mat4(1 / (tan(fov / 2)*asp), 0, 0, 0,
			0, 1 / tan(fov / 2), 0, 0,
			0, 0, -(fp + bp) / (bp - fp), -1,
			0, 0, -2 * fp*bp / (bp - fp), 0);
	}
	void SetUniform() {
		int location = glGetUniformLocation(gpuProgram->getId(), "wEye");
		if (location >= 0) glUniform3fv(location, 1, &wEye.x);
		else printf("uniform wEye cannot be set\n");
	}
};

Camera camera; // 3D camera

//-------------------------- -
struct Material {
	//---------------------------
	vec3 kd, ks, ka;
	float shininess;

	void SetUniform() {
		kd.SetUniform(gpuProgram->getId(), "kd");
		ks.SetUniform(gpuProgram->getId(), "ks");
		ka.SetUniform(gpuProgram->getId(), "ka");
		int location = glGetUniformLocation(gpuProgram->getId(), "shine");
		if (location >= 0) glUniform1f(location, shininess); else printf("uniform shininess cannot be set\n");
	}
};

//---------------------------
struct Light {
	//---------------------------
	vec3 La, Le;
	vec3 wLightDir;

	Light() : La(1, 1, 1), Le(3, 3, 3) { }
	void SetUniform(bool enable) {
		if (enable) {
			La.SetUniform(gpuProgram->getId(), "La");
			Le.SetUniform(gpuProgram->getId(), "Le");
		}
		else {
			vec3(0, 0, 0).SetUniform(gpuProgram->getId(), "La");
			vec3(0, 0, 0).SetUniform(gpuProgram->getId(), "Le");
		}
		wLightDir.SetUniform(gpuProgram->getId(), "wLiDir");
	}
};


//---------------------------
struct VertexData {
	//---------------------------
	vec3 position, normal;
	vec2 texcoord;
};

//---------------------------
class Geometry {
	//---------------------------
	unsigned int vao, type;        // vertex array object
protected:
	int nVertices;
public:
	Geometry(unsigned int _type) {
		type = _type;
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
	}
	void Draw(mat4 M, mat4 Minv) {
		mat4 MVP = M * camera.V() * camera.P();
		MVP.SetUniform(gpuProgram->getId(), "MVP");
		M.SetUniform(gpuProgram->getId(), "M");
		Minv.SetUniform(gpuProgram->getId(), "Minv");
		glBindVertexArray(vao);
		glDrawArrays(type, 0, nVertices);
	}
};

//---------------------------
class ParamSurface : public Geometry {
	//---------------------------
public:
	ParamSurface() : Geometry(GL_TRIANGLES) {}

	virtual VertexData GenVertexData(float u, float v) = 0;

	void Create(int N = 16, int M = 16) {
		unsigned int vbo;
		glGenBuffers(1, &vbo); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		nVertices = N * M * 6;
		std::vector<VertexData> vtxData;	// vertices on the CPU
		for (int i = 0; i < N; i++) {
			for (int j = 0; j < M; j++) {
				vtxData.push_back(GenVertexData((float)i / N, (float)j / M));
				vtxData.push_back(GenVertexData((float)(i + 1) / N, (float)j / M));
				vtxData.push_back(GenVertexData((float)i / N, (float)(j + 1) / M));
				vtxData.push_back(GenVertexData((float)(i + 1) / N, (float)j / M));
				vtxData.push_back(GenVertexData((float)(i + 1) / N, (float)(j + 1) / M));
				vtxData.push_back(GenVertexData((float)i / N, (float)(j + 1) / M));
			}
		}
		glBufferData(GL_ARRAY_BUFFER, nVertices * sizeof(VertexData), &vtxData[0], GL_STATIC_DRAW);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0 = POSITION
		glEnableVertexAttribArray(1);  // attribute array 1 = NORMAL
		glEnableVertexAttribArray(2);  // attribute array 2 = TEXCOORD0
		// attribute array, components/attribute, component type, normalize?, stride, offset
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, position));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, normal));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, texcoord));
	}
};

//---------------------------
class Sphere : public ParamSurface {
	//---------------------------
	float r;
public:
	Sphere(float _r) {
		r = _r;
		Create(20, 20);
	}

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		vd.normal = vec3(cosf(u * 2.0f * M_PI) * sin(v*M_PI), sinf(u * 2.0f * M_PI) * sinf(v*M_PI), cosf(v*M_PI));
		vd.position = vd.normal * r;
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
class TruncatedCone : public ParamSurface {
	//---------------------------
	float rStart, rEnd;
public:
	TruncatedCone(float _rStart, float _rEnd) {
		rStart = _rStart, rEnd = _rEnd;
		Create(20, 20);
	}

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		float U = u * 2.0f * M_PI;
		vec3 circle = vec3(cosf(U), sinf(U), 0);
		vd.position = circle * (rStart * (1 - v) + rEnd * v) + vec3(0, 0, v);
		vec3 drdU = vec3(-sinf(U), cosf(U));
		vec3 drdv = circle * (rEnd - rStart) + vec3(0, 0, 1);
		vd.normal = cross(drdU, drdv);
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
class Quad : public ParamSurface {
	//---------------------------
	float size;
public:
	Quad() {
		size = 100;
		Create(20, 20);
	}

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		vd.normal = vec3(0, 1, 0);
		vd.position = vec3((u - 0.5) * 2, 0, (v - 0.5) * 2) * size;
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
class Floor {
	//---------------------------
	Material * material;
	Geometry * quad;
public:
	Floor(Material * _m) {
		material = _m;
		quad = new Quad();
	}
	void Draw(mat4 M, mat4 Minv) {
		material->SetUniform();
		quad->Draw(M, Minv);
	}
};

const float boneRadius = 0.5;
const float legLength = 5;

#define INVERSE_KINEMATICS
//===============================================================
class PrimitiveMan {
	//===============================================================
	Material * material;
	Sphere * head;
	TruncatedCone * torso;
	Sphere * joint;
	TruncatedCone * bone;

	float dleftarm_angle, drightarm_angle, dleftleg_angle, drightleg_angle;
	float leftLegAngle, rightLegAngle, leftArmAngle, rightArmAngle, leftToeAngle, rightToeAngle;
	float forward, up;          // movement
public:
	PrimitiveMan(Material * _m) {
		material = _m;
		head = new Sphere(1.5);
		torso = new TruncatedCone(1.0, 0.8);
		joint = new Sphere(boneRadius);
		bone = new TruncatedCone(boneRadius, boneRadius / 5);
		forward = 0;
		up = legLength + boneRadius;

		dleftarm_angle = -6; drightarm_angle = 6;
		dleftleg_angle = 3;  drightleg_angle = -3;

		rightLegAngle = 120;
		rightToeAngle = -120;
		leftLegAngle = 60;
		leftToeAngle = -60;
		rightArmAngle = 30;
		leftArmAngle = 150;
	}
	float Forward() { return forward; }

	void Animate(float dt) {
		if (forward < 105) {
			float oldleg_angle = rightLegAngle;

			leftArmAngle += dleftarm_angle * dt;
			rightArmAngle += drightarm_angle * dt;
			leftLegAngle += dleftleg_angle * dt;
			rightLegAngle += drightleg_angle * dt;
			if (leftArmAngle > 150) { dleftarm_angle = -6; drightarm_angle = 6; }
			if (rightArmAngle > 150) { dleftarm_angle = 6; drightarm_angle = -6; }
			if (leftLegAngle > 120) { dleftleg_angle = -3; drightleg_angle = 3; }
			if (rightLegAngle > 120) { dleftleg_angle = 3; drightleg_angle = -3; }
			// "inverse kinematics"
#ifdef INVERSE_KINEMATICS
			forward += fabs(legLength * (sin((rightLegAngle - 90) * M_PI / 180) - sin((oldleg_angle - 90) * M_PI / 180)));
			up = legLength * cos((rightLegAngle - 90) * M_PI / 180) + boneRadius;
			leftToeAngle = -leftLegAngle;
			rightToeAngle = -rightLegAngle;
#else
			forward += 0.3 * dt;
#endif
		}
		else {
			up -= 2 * dt;
		}
	}
	void DrawHead(mat4 M, mat4 Minv) {
		M = TranslateMatrix(vec3(0, 6.5f, 0)) * M;
		Minv = Minv * TranslateMatrix(-vec3(0, 6.5f, 0));
		head->Draw(M, Minv);
	}
	void DrawTorso(mat4 M, mat4 Minv) {
		M = ScaleMatrix(vec3(2, 1, 5)) * RotationMatrix(90 * M_PI / 180, vec3(1, 0, 0)) * TranslateMatrix(vec3(0, 5, 0)) * M;
		Minv = Minv * TranslateMatrix(-vec3(0, 5, 0)) * RotationMatrix(-90 * M_PI / 180, vec3(1, 0, 0)) * ScaleMatrix(vec3(0.5, 1, 0.2));
		torso->Draw(M, Minv);
	}
	void DrawLeftLeg(mat4 M, mat4 Minv) {
		joint->Draw(M, Minv);

		M = RotationMatrix(leftLegAngle * M_PI / 180, vec3(1, 0, 0)) * M;
		Minv = Minv * RotationMatrix(-leftLegAngle * M_PI / 180, vec3(1, 0, 0));
		bone->Draw(ScaleMatrix(vec3(1, 1, legLength)) * TranslateMatrix(vec3(0, 0, boneRadius)) * M,
			Minv * TranslateMatrix(-vec3(0, 0, boneRadius)) * ScaleMatrix(vec3(1, 1, 1 / legLength)));

		DrawToe(RotationMatrix(-leftLegAngle * M_PI / 180, vec3(1, 0, 0)) * TranslateMatrix(vec3(0, 0, legLength)) * M,
			Minv * TranslateMatrix(-vec3(0, 0, legLength)) * RotationMatrix(leftLegAngle * M_PI / 180, vec3(1, 0, 0)));
	}

	void DrawRightLeg(mat4 M, mat4 Minv) {
		joint->Draw(M, Minv);

		M = RotationMatrix(rightLegAngle * M_PI / 180, vec3(1, 0, 0)) * M;
		Minv = Minv * RotationMatrix(-rightLegAngle * M_PI / 180, vec3(1, 0, 0));
		const float legLength = 5;
		bone->Draw(ScaleMatrix(vec3(1, 1, legLength)) * TranslateMatrix(vec3(0, 0, boneRadius)) * M,
			Minv * TranslateMatrix(-vec3(0, 0, boneRadius)) * ScaleMatrix(vec3(1, 1, 1 / legLength)));

		DrawToe(RotationMatrix(-rightLegAngle * M_PI / 180, vec3(1, 0, 0)) * TranslateMatrix(vec3(0, 0, legLength)) *M,
			Minv  * TranslateMatrix(-vec3(0, 0, legLength))* RotationMatrix(rightLegAngle * M_PI / 180, vec3(1, 0, 0)));
	}

	void DrawToe(mat4 M, mat4 Minv) {
		joint->Draw(M, Minv);
		const float toeLength = 1;
		bone->Draw(ScaleMatrix(vec3(1, 1, toeLength)) * TranslateMatrix(vec3(0, 0, boneRadius)) * M,
			Minv * TranslateMatrix(-vec3(0, 0, boneRadius)) * ScaleMatrix(vec3(1, 1, 1 / toeLength)));
	}

	void DrawArm(mat4 M, mat4 Minv) {
		joint->Draw(M, Minv);
		const float toeLength = 1;
		bone->Draw(ScaleMatrix(vec3(1, 1, 4)) * TranslateMatrix(vec3(0, 0, boneRadius)) * M,
			Minv * TranslateMatrix(-vec3(0, 0, boneRadius)) * ScaleMatrix(vec3(1, 1, 1.0 / 4)));
	}

	void Draw(mat4 M, mat4 Minv) {     // Draw the hierarchy
		M = TranslateMatrix(vec3(0, up, forward)) * M;
		Minv = Minv * TranslateMatrix(-vec3(0, up, forward));
		material->SetUniform();
		DrawHead(M, Minv);
		DrawTorso(M, Minv);

		vec3 rightLegJoint(-2, 0, 0);
		DrawRightLeg(TranslateMatrix(rightLegJoint) * M, Minv * TranslateMatrix(-rightLegJoint));

		vec3 leftLegJoint(2, 0, 0);
		DrawLeftLeg(TranslateMatrix(leftLegJoint) * M, Minv * TranslateMatrix(-leftLegJoint));

		vec3 rightArmJoint(-2.4, 5, 0);
		DrawArm(RotationMatrix(rightArmAngle * M_PI / 180, vec3(1, 0, 0)) * TranslateMatrix(rightArmJoint) * M,
			Minv * TranslateMatrix(-rightArmJoint) * RotationMatrix(-rightArmAngle * M_PI / 180, vec3(1, 0, 0)));

		vec3 leftArmJoint(2.4, 5, 0);
		DrawArm(RotationMatrix(leftArmAngle * M_PI / 180, vec3(1, 0, 0)) * TranslateMatrix(leftArmJoint) * M,
			Minv * TranslateMatrix(-leftArmJoint) * RotationMatrix(-leftArmAngle * M_PI / 180, vec3(1, 0, 0)));
	}
};

//---------------------------
class Scene {
	//---------------------------
	PrimitiveMan * pman;
	Floor * floor;
public:
	Light light;

	void Build() {
		// Materials
		Material * material0 = new Material;
		material0->kd = vec3(0.2f, 0.3f, 1);
		material0->ks = vec3(1, 1, 1);
		material0->ka = vec3(0.2f, 0.3f, 1);
		material0->shininess = 20;

		Material * material1 = new Material;
		material1->kd = vec3(0, 1, 1);
		material1->ks = vec3(2, 2, 2);
		material1->ka = vec3(0.2f, 0.2f, 0.2f);
		material1->shininess = 200;

		// Geometries
		pman = new PrimitiveMan(material0);
		floor = new Floor(material1);

		// Camera
		camera.wEye = vec3(0, 0, 4);
		camera.wLookat = vec3(0, 0, 0);
		camera.wVup = vec3(0, 1, 0);

		// Light
		light.wLightDir = vec3(5, 5, 4);

	}
	void Render() {
		camera.SetUniform();
		light.SetUniform(true);

		mat4 unit = TranslateMatrix(vec3(0, 0, 0));
		floor->Draw(unit, unit);

		pman->Draw(unit, unit);
		// shadow matrix that projects the man onto the floor

		light.SetUniform(false);

		mat4 shadowMatrix = { 1, 0, 0, 0,
			-light.wLightDir.x / light.wLightDir.y, 0, -light.wLightDir.z / light.wLightDir.y, 0,
			0, 0, 1, 0,
			0, 0.001f, 0, 1 };
		pman->Draw(shadowMatrix, shadowMatrix);
	}

	void Animate(float t) {
		static float tprev = 0;
		float dt = t - tprev;
		tprev = t;

		pman->Animate(dt);

		static float cam_angle = 0;
		cam_angle += 0.01 * dt;			// camera rotate

		const float camera_rad = 30;
		camera.wEye = vec3(cos(cam_angle) * camera_rad, 10, sin(cam_angle) * camera_rad + pman->Forward());
		camera.wLookat = vec3(0, 0, pman->Forward());
	}
};

Scene scene;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	gpuProgram = new PhongShader();
	scene.Build();
}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0.5f, 0.5f, 0.8f, 1.0f);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	scene.Render();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) { }

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) { }

// Mouse click event
void onMouse(int button, int state, int pX, int pY) { }

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
	long time = glutGet(GLUT_ELAPSED_TIME); // elapsed time since the start of the program
	float sec = time / 30.0f;				// convert msec to sec
	scene.Animate(sec);					// animate the camera
	glutPostRedisplay();					// redraw the scene
}