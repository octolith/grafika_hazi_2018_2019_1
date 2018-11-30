//=============================================================================================
// Computer Graphics Sample Program: 3D engine-let
// Shader: Gouraud, Phong, NPR
// Material: diffuse + Phong-Blinn
// Texture: CPU-procedural
// Geometry: sphere, torus, mobius
// Camera: perspective
// Light: point
//=============================================================================================
#include "framework.h"

const int tessellationLevel = 20;

//---------------------------
struct Camera { // 3D camera
//---------------------------
	vec3 wEye, wLookat, wVup;   // extinsic
	float fov, asp, fp, bp;		// intrinsic
public:
	Camera() {
		asp = (float)windowWidth / windowHeight;
		fov = 75.0f * (float)M_PI / 180.0f;
		fp = 1; bp = 10;
	}
	mat4 V() { // view matrix: translates the center to the origin
		vec3 w = normalize(wEye - wLookat);
		vec3 u = normalize(cross(wVup, w));
		vec3 v = cross(w, u);
		return TranslateMatrix(wEye * (-1)) * mat4(u.x, v.x, w.x, 0,
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
	void Animate(float t) { }
};

//---------------------------
struct Material {
	//---------------------------
	vec3 kd, ks, ka;
	float shininess;

	void SetUniform(unsigned shaderProg, char * name) {
		char buffer[256];
		sprintf(buffer, "%s.kd", name);
		kd.SetUniform(shaderProg, buffer);

		sprintf(buffer, "%s.ks", name);
		ks.SetUniform(shaderProg, buffer);

		sprintf(buffer, "%s.ka", name);
		ka.SetUniform(shaderProg, buffer);

		sprintf(buffer, "%s.shininess", name);
		int location = glGetUniformLocation(shaderProg, buffer);
		if (location >= 0) glUniform1f(location, shininess); else printf("uniform shininess cannot be set\n");
	}
};

//---------------------------
struct Light {
	//---------------------------
	vec3 La, Le;
	vec4 wLightPos;

	void Animate(float t) {	}

	void SetUniform(unsigned shaderProg, char * name) {
		char buffer[256];
		sprintf(buffer, "%s.La", name);
		La.SetUniform(shaderProg, buffer);

		sprintf(buffer, "%s.Le", name);
		Le.SetUniform(shaderProg, buffer);

		sprintf(buffer, "%s.wLightPos", name);
		wLightPos.SetUniform(shaderProg, buffer);
	}
};

//---------------------------
struct CheckerBoardTexture : public Texture {
	//---------------------------
	CheckerBoardTexture(const int width = 0, const int height = 0) : Texture() {
		glBindTexture(GL_TEXTURE_2D, textureId);    // binding
		std::vector<vec3> image(width * height);
		const vec3 yellow(1, 1, 0), blue(0, 0, 1);
		for (int x = 0; x < width; x++) for (int y = 0; y < height; y++) {
			image[y * width + x] = (x & 1) ^ (y & 1) ? yellow : blue;
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_FLOAT, &image[0]); //Texture->OpenGL
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
};

//---------------------------
struct RenderState {
	//---------------------------
	mat4	           MVP, M, Minv, V, P;
	Material *         material;
	std::vector<Light> lights;
	Texture *          texture;
	vec3	           wEye;
};

//---------------------------
class Shader : public GPUProgram {
	//--------------------------
public:
	virtual void Bind(RenderState state) = 0;
};

//---------------------------
class GouraudShader : public Shader {
	//---------------------------
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		struct Light {
			vec3 La, Le;
			vec4 wLightPos;
		};
		
		struct Material {
			vec3 kd, ks, ka;
			float shininess;
		};

		uniform mat4  MVP, M, Minv;  // MVP, Model, Model-inverse
		uniform Light[8] lights;     // light source direction 
		uniform int   nLights;
		uniform vec3  wEye;          // pos of eye
		uniform Material  material;  // diffuse, specular, ambient ref

		layout(location = 0) in vec3  vtxPos;            // pos in modeling space
		layout(location = 1) in vec3  vtxNorm;      	 // normal in modeling space

		out vec3 radiance;		    // reflected radiance

		void main() {
			gl_Position = vec4(vtxPos, 1) * MVP; // to NDC
			// radiance computation
			vec4 wPos = vec4(vtxPos, 1) * M;	
			vec3 V = normalize(wEye * wPos.w - wPos.xyz);
			vec3 N = normalize((Minv * vec4(vtxNorm, 0)).xyz);
			if (dot(N, V) < 0) N = -N;	// prepare for one-sided surfaces like Mobius or Klein

			radiance = vec3(0, 0, 0);
			for(int i = 0; i < nLights; i++) {
				vec3 L = normalize(lights[i].wLightPos.xyz * wPos.w - wPos.xyz * lights[i].wLightPos.w);
				vec3 H = normalize(L + V);
				float cost = max(dot(N,L), 0), cosd = max(dot(N,H), 0);
				radiance += material.ka * lights[i].La + (material.kd * cost + material.ks * pow(cosd, material.shininess)) * lights[i].Le;
			}
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		in  vec3 radiance;      // interpolated radiance
		out vec4 fragmentColor; // output goes to frame buffer

		void main() {
			fragmentColor = vec4(radiance, 1);
		}
	)";
public:
	GouraudShader() { Create(vertexSource, fragmentSource, "fragmentColor"); }

	void Bind(RenderState state) {
		glUseProgram(getId()); 		// make this program run
		state.MVP.SetUniform(getId(), "MVP");
		state.M.SetUniform(getId(), "M");
		state.Minv.SetUniform(getId(), "Minv");
		state.wEye.SetUniform(getId(), "wEye");
		state.material->SetUniform(getId(), "material");

		int location = glGetUniformLocation(getId(), "nLights");
		if (location >= 0) glUniform1i(location, state.lights.size()); else printf("uniform nLight cannot be set\n");
		for (int i = 0; i < state.lights.size(); i++) {
			char buffer[256];
			sprintf(buffer, "lights[%d]", i);
			state.lights[i].SetUniform(getId(), buffer);
		}
	}
};

//---------------------------
class PhongShader : public Shader {
	//---------------------------
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		struct Light {
			vec3 La, Le;
			vec4 wLightPos;
		};

		uniform mat4  MVP, M, Minv; // MVP, Model, Model-inverse
		uniform Light[8] lights;    // light sources 
		uniform int   nLights;
		uniform vec3  wEye;         // pos of eye

		layout(location = 0) in vec3  vtxPos;            // pos in modeling space
		layout(location = 1) in vec3  vtxNorm;      	 // normal in modeling space
		layout(location = 2) in vec2  vtxUV;

		out vec3 wNormal;		    // normal in world space
		out vec3 wView;             // view in world space
		out vec3 wLight[8];		    // light dir in world space
		out vec2 texcoord;

		void main() {
			gl_Position = vec4(vtxPos, 1) * MVP; // to NDC
			// vectors for radiance computation
			vec4 wPos = vec4(vtxPos, 1) * M;
			for(int i = 0; i < nLights; i++) {
				wLight[i] = lights[i].wLightPos.xyz * wPos.w - wPos.xyz * lights[i].wLightPos.w;
			}
			wView  = wEye * wPos.w - wPos.xyz;
			wNormal = (Minv * vec4(vtxNorm, 0)).xyz;
			texcoord = vtxUV;
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		struct Light {
			vec3 La, Le;
			vec4 wLightPos;
		};

		struct Material {
			vec3 kd, ks, ka;
			float shininess;
		};

		uniform Material material;
		uniform Light[8] lights;    // light sources 
		uniform int   nLights;
		uniform sampler2D diffuseTexture;

		in  vec3 wNormal;       // interpolated world sp normal
		in  vec3 wView;         // interpolated world sp view
		in  vec3 wLight[8];     // interpolated world sp illum dir
		in  vec2 texcoord;
		
		out vec4 fragmentColor; // output goes to frame buffer

		void main() {
			vec3 N = normalize(wNormal);
			vec3 V = normalize(wView); 
			if (dot(N, V) < 0) N = -N;	// prepare for one-sided surfaces like Mobius or Klein
			vec3 texColor = texture(diffuseTexture, texcoord).rgb;
			vec3 ka = material.ka * texColor;
			vec3 kd = material.kd * texColor;

			vec3 radiance = vec3(0, 0, 0);
			for(int i = 0; i < nLights; i++) {
				vec3 L = normalize(wLight[i]);
				vec3 H = normalize(L + V);
				float cost = max(dot(N,L), 0), cosd = max(dot(N,H), 0);
				// kd and ka are modulated by the texture
				radiance += ka * lights[i].La + (kd * cost + material.ks * pow(cosd, material.shininess)) * lights[i].Le;
			}
			fragmentColor = vec4(radiance, 1);
		}
	)";
public:
	PhongShader() { Create(vertexSource, fragmentSource, "fragmentColor"); }

	void Bind(RenderState state) {
		glUseProgram(getId()); 		// make this program run
		state.MVP.SetUniform(getId(), "MVP");
		state.M.SetUniform(getId(), "M");
		state.Minv.SetUniform(getId(), "Minv");
		state.wEye.SetUniform(getId(), "wEye");
		state.material->SetUniform(getId(), "material");

		int location = glGetUniformLocation(getId(), "nLights");
		if (location >= 0) glUniform1i(location, state.lights.size()); else printf("uniform nLight cannot be set\n");
		for (int i = 0; i < state.lights.size(); i++) {
			char buffer[256];
			sprintf(buffer, "lights[%d]", i);
			state.lights[i].SetUniform(getId(), buffer);
		}
		state.texture->SetUniform(getId(), "diffuseTexture");
	}
};

//---------------------------
class NPRShader : public Shader {
	//---------------------------
	const char * vertexSource = R"(
		#version 330
		precision highp float;

		uniform mat4  MVP, M, Minv; // MVP, Model, Model-inverse
		uniform	vec4  wLightPos;
		uniform vec3  wEye;         // pos of eye

		layout(location = 0) in vec3  vtxPos;            // pos in modeling space
		layout(location = 1) in vec3  vtxNorm;      	 // normal in modeling space
		layout(location = 2) in vec2  vtxUV;

		out vec3 wNormal, wView, wLight;				// in world space
		out vec2 texcoord;

		void main() {
		   gl_Position = vec4(vtxPos, 1) * MVP; // to NDC
		   vec4 wPos = vec4(vtxPos, 1) * M;
		   wLight = wLightPos.xyz * wPos.w - wPos.xyz * wLightPos.w;
		   wView  = wEye * wPos.w - wPos.xyz;
		   wNormal = (Minv * vec4(vtxNorm, 0)).xyz;
		   texcoord = vtxUV;
		}
	)";

	// fragment shader in GLSL
	const char * fragmentSource = R"(
		#version 330
		precision highp float;

		uniform sampler2D diffuseTexture;

		in  vec3 wNormal, wView, wLight;	// interpolated
		in  vec2 texcoord;
		out vec4 fragmentColor;    			// output goes to frame buffer

		void main() {
		   vec3 N = normalize(wNormal), V = normalize(wView), L = normalize(wLight);
		   float y = (dot(N, L) > 0.5) ? 1 : 0.5;
		   if (abs(dot(N, V)) < 0.2) fragmentColor = vec4(0, 0, 0, 1);
		   else						 fragmentColor = vec4(y * texture(diffuseTexture, texcoord).rgb, 1);
		}
	)";
public:
	NPRShader() { Create(vertexSource, fragmentSource, "fragmentColor"); }

	void Bind(RenderState state) {
		glUseProgram(getId()); 		// make this program run
		state.MVP.SetUniform(getId(), "MVP");
		state.M.SetUniform(getId(), "M");
		state.Minv.SetUniform(getId(), "Minv");
		state.wEye.SetUniform(getId(), "wEye");
		state.lights[0].wLightPos.SetUniform(getId(), "wLightPos");

		state.texture->SetUniform(getId(), "diffuseTexture");
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
protected:
	unsigned int vao;        // vertex array object
public:
	Geometry() {
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
	}
	virtual void Draw() = 0;
};

//---------------------------
class ParamSurface : public Geometry {
	//---------------------------
	unsigned int nVtxPerStrip, nStrips;
public:
	ParamSurface() {
		nVtxPerStrip = nStrips = 0;
	}
	virtual VertexData GenVertexData(float u, float v) = 0;

	void Create(int N = tessellationLevel, int M = tessellationLevel) {
		unsigned int vbo;
		glGenBuffers(1, &vbo); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		nVtxPerStrip = (M + 1) * 2;
		nStrips = N;
		std::vector<VertexData> vtxData;	// vertices on the CPU
		for (int i = 0; i < N; i++) {
			for (int j = 0; j <= M; j++) {
				vtxData.push_back(GenVertexData((float)j / M, (float)i / N));
				vtxData.push_back(GenVertexData((float)j / M, (float)(i + 1) / N));
			}
		}
		glBufferData(GL_ARRAY_BUFFER, nVtxPerStrip * nStrips * sizeof(VertexData), &vtxData[0], GL_STATIC_DRAW);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0 = POSITION
		glEnableVertexAttribArray(1);  // attribute array 1 = NORMAL
		glEnableVertexAttribArray(2);  // attribute array 2 = TEXCOORD0
		// attribute array, components/attribute, component type, normalize?, stride, offset
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, position));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, normal));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, texcoord));
	}
	void Draw() {
		glBindVertexArray(vao);
		for (int i = 0; i < nStrips; i++) glDrawArrays(GL_TRIANGLE_STRIP, i *  nVtxPerStrip, nVtxPerStrip);
	}
};

//---------------------------
struct Clifford {
	//---------------------------
	float f, d;
	Clifford(float f0 = 0, float d0 = 0) { f = f0, d = d0; }
	Clifford operator+(Clifford r) { return Clifford(f + r.f, d + r.d); }
	Clifford operator-(Clifford r) { return Clifford(f - r.f, d - r.d); }
	Clifford operator*(Clifford r) { return Clifford(f * r.f, f * r.d + d * r.f); }
	Clifford operator/(Clifford r) {
		float l = r.f * r.f;
		return (*this) * Clifford(r.f / l, -r.d / l);
	}
};

Clifford T(float t) { return Clifford(t, 1); }
Clifford Sin(Clifford g) { return Clifford(sin(g.f), cos(g.f) * g.d); }
Clifford Cos(Clifford g) { return Clifford(cos(g.f), -sin(g.f) * g.d); }
Clifford Tan(Clifford g) { return Sin(g) / Cos(g); }
Clifford Log(Clifford g) { return Clifford(logf(g.f), 1 / g.f * g.d); }
Clifford Exp(Clifford g) { return Clifford(expf(g.f), expf(g.f) * g.d); }
Clifford Pow(Clifford g, float n) { return Clifford(powf(g.f, n), n * powf(g.f, n - 1) * g.d); }

//---------------------------
class Sphere : public ParamSurface {
	//---------------------------
public:
	Sphere() { Create(); }

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		vd.position = vd.normal = vec3(cosf(u * 2.0f * M_PI) * sinf(v*M_PI), sinf(u * 2.0f * M_PI) * sinf(v*M_PI), cosf(v*M_PI));
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
class Torus : public ParamSurface {
	//---------------------------
	const float R = 1, r = 0.5;

	vec3 Point(float u, float v, float rr) {
		float ur = u * 2.0f * M_PI, vr = v * 2.0f * M_PI;
		float l = R + rr * cosf(ur);
		return vec3(l * cosf(vr), l * sinf(vr), rr * sinf(ur));
	}
public:
	Torus() { Create(); }

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		vd.position = Point(u, v, r);
		vd.normal = (vd.position - Point(u, v, 0)) * (1.0f / r);
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
class Mobius : public ParamSurface {
	//---------------------------
	float R, w;
public:
	Mobius() { R = 1; w = 0.5; Create(); }

	VertexData GenVertexData(float u, float v) {
		VertexData vd;
		float U = u * M_PI, V = (v - 0.5) * w;
		Clifford x = (Cos(T(U)) * V + R) * Cos(T(U) * 2);
		Clifford y = (Cos(T(U)) * V + R) * Sin(T(U) * 2);
		Clifford z = Sin(T(U)) * V;
		vd.position = vec3(x.f, y.f, z.f);
		vec3 drdU(x.d, y.d, z.d);
		vec3 drdV(cos(U)*cos(2 * U), cos(U)*sin(2 * U), sin(U));
		vd.normal = cross(drdU, drdV);
		vd.texcoord = vec2(u, v);
		return vd;
	}
};

//---------------------------
struct Object {
	//---------------------------
	Shader * shader;
	Material * material;
	Texture * texture;
	Geometry * geometry;
	vec3 scale, translation, rotationAxis;
	float rotationAngle;
public:
	Object(Shader * _shader, Material * _material, Texture * _texture, Geometry * _geometry) :
		scale(vec3(1, 1, 1)), translation(vec3(0, 0, 0)), rotationAxis(0, 0, 1), rotationAngle(0) {
		shader = _shader;
		texture = _texture;
		material = _material;
		geometry = _geometry;
	}

	void Draw(RenderState state) {
		state.M = ScaleMatrix(scale) * RotationMatrix(rotationAngle, rotationAxis) * TranslateMatrix(translation);
		state.Minv = TranslateMatrix(-translation) * RotationMatrix(-rotationAngle, rotationAxis) * ScaleMatrix(vec3(1 / scale.x, 1 / scale.y, 1 / scale.z));
		state.MVP = state.M * state.V * state.P;
		state.material = material;
		state.texture = texture;
		shader->Bind(state);
		geometry->Draw();
	}

	void Animate(float tstart, float tend) { rotationAngle = 0.8 * tend; }
};

//---------------------------
class Scene {
	//---------------------------
	std::vector<Object *> objects;
public:
	Camera camera; // 3D camera
	std::vector<Light> lights;

	void Build() {
		// Shaders
		Shader * phongShader = new PhongShader();
		Shader * gouraudShader = new GouraudShader();
		Shader * nprShader = new NPRShader();

		// Materials
		Material * material0 = new Material;
		material0->kd = vec3(0.6f, 0.4f, 0.2f);
		material0->ks = vec3(4, 4, 4);
		material0->ka = vec3(0.1f, 0.1f, 0.1f);
		material0->shininess = 100;

		Material * material1 = new Material;
		material1->kd = vec3(0.8, 0.6, 0.4);
		material1->ks = vec3(0.3, 0.3, 0.3);
		material1->ka = vec3(0.2f, 0.2f, 0.2f);
		material1->shininess = 30;

		// Textures
		Texture * texture4x8 = new CheckerBoardTexture(4, 8);
		Texture * texture15x20 = new CheckerBoardTexture(15, 20);

		// Geometries
		Geometry * sphere = new Sphere();
		Geometry * torus = new Torus();
		Geometry * mobius = new Mobius();

		// Create objects by setting up their vertex data on the GPU
		Object * sphereObject1 = new Object(phongShader, material0, texture15x20, sphere);
		sphereObject1->translation = vec3(-3, 3, 0);
		sphereObject1->rotationAxis = vec3(0, 1, 1);
		sphereObject1->scale = vec3(0.5f, 1.2f, 0.5f);
		objects.push_back(sphereObject1);

		Object * torusObject1 = new Object(phongShader, material0, texture4x8, torus);
		torusObject1->translation = vec3(0, 3, 0);
		torusObject1->rotationAxis = vec3(1, 1, -1);
		torusObject1->scale = vec3(0.7f, 0.7f, 0.7f);
		objects.push_back(torusObject1);

		Object * mobiusObject1 = new Object(phongShader, material0, texture4x8, mobius);
		mobiusObject1->translation = vec3(3, 3, 0);
		mobiusObject1->rotationAxis = vec3(1, 0, 0);
		mobiusObject1->scale = vec3(0.7f, 0.7f, 0.7f);
		objects.push_back(mobiusObject1);

		Object * sphereObject2 = new Object(*sphereObject1);
		sphereObject2->translation = vec3(-3, -3, 0);
		sphereObject2->shader = nprShader;
		objects.push_back(sphereObject2);

		Object * torusObject2 = new Object(*torusObject1);
		torusObject2->translation = vec3(0, -3, 0);
		torusObject2->shader = nprShader;
		objects.push_back(torusObject2);

		Object * mobiusObject2 = new Object(*mobiusObject1);
		mobiusObject2->translation = vec3(3, -3, 0);
		mobiusObject2->shader = nprShader;
		objects.push_back(mobiusObject2);

		Object * sphereObject3 = new Object(*sphereObject1);
		sphereObject3->translation = vec3(-3, 0, 0);
		sphereObject3->shader = gouraudShader;
		objects.push_back(sphereObject3);

		Object * torusObject3 = new Object(*torusObject1);
		torusObject3->translation = vec3(0, 0, 0);
		torusObject3->shader = gouraudShader;
		objects.push_back(torusObject3);

		Object * mobiusObject3 = new Object(*mobiusObject1);
		mobiusObject3->translation = vec3(3, 0, 0);
		mobiusObject3->shader = gouraudShader;
		objects.push_back(mobiusObject3);

		// Camera
		camera.wEye = vec3(0, 0, 6);
		camera.wLookat = vec3(0, 0, 0);
		camera.wVup = vec3(0, 1, 0);

		// Lights
		lights.resize(3);
		lights[0].wLightPos = vec4(5, 5, 4, 0);	// ideal point -> directional light source
		lights[0].La = vec3(0.1, 0.1, 1);
		lights[0].Le = vec3(3, 0, 0);

		lights[1].wLightPos = vec4(5, 10, 20, 0);	// ideal point -> directional light source
		lights[1].La = vec3(0.2, 0.2, 0.2);
		lights[1].Le = vec3(0, 3, 0);

		lights[2].wLightPos = vec4(-5, 5, 5, 0);	// ideal point -> directional light source
		lights[2].La = vec3(0.1, 0.1, 0.1);
		lights[2].Le = vec3(0, 0, 3);

	}
	void Render() {
		RenderState state;
		state.wEye = camera.wEye;
		state.V = camera.V();
		state.P = camera.P();
		state.lights = lights;
		for (Object * obj : objects) obj->Draw(state);
	}

	void Animate(float tstart, float tend) {
		camera.Animate(tend);
		for (int i = 0; i < lights.size(); i++) { lights[i].Animate(tend); }
		for (Object * obj : objects) obj->Animate(tstart, tend);
	}
};

Scene scene;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
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
	static float tend = 0;
	const float dt = 0.1; // dt is ”infinitesimal”
	float tstart = tend;
	tend = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;

	for (float t = tstart; t < tend; t += dt) {
		float Dt = fmin(dt, tend - t);
		scene.Animate(t, t + Dt);
	}
	glutPostRedisplay();
}