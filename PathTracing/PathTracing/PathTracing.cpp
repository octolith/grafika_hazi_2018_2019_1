//=============================================================================================
// Path tracing program
//=============================================================================================
#define _CRT_SECURE_NO_WARNINGS
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

const unsigned int screenWidth = 600, screenHeight = 600;	// resolution of the rendered image
const double epsilon = 1e-5;	// limit of considering a number to be zero
const int maxdepth = 10;		// max depth of recursion
const int nSamples = 50;		// number of path samples per pixel
const int nMaxObjects = 10;		// maximum number of objects
const int nMaxLights = 10;		// maximum number of light sources

// 3D vector operations
struct vec3 {
	double x, y, z;
	vec3(double x0 = 0, double y0 = 0, double z0 = 0) { x = x0; y = y0; z = z0; }
	vec3 operator*(double a) const { return vec3(x * a, y * a, z * a); }
	vec3 operator/(double d) const { return vec3(x / d, y / d, z / d); }
	vec3 operator+(const vec3& v) const { return vec3(x + v.x, y + v.y, z + v.z); }
	void operator+=(const vec3& v) { x += v.x; y += v.y; z += v.z; }
	vec3 operator-(const vec3& v) const { return vec3(x - v.x, y - v.y, z - v.z); }
	vec3 operator*(const vec3& v) const { return vec3(x * v.x, y * v.y, z * v.z); }
	vec3 operator-() const { return vec3(-x, -y, -z); }
	vec3 normalize() const { return (*this) * (1 / (Length() + epsilon)); }
	double Length() const { return sqrt(x * x + y * y + z * z); }
	double average() { return (x + y + z) / 3; }
};

double dot(const vec3& v1, const vec3& v2) {	// dot product
	return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
}

vec3 cross(const vec3& v1, const vec3& v2) {	// cross product
	return vec3(v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
}

// Pseudo-random number in [0,1)
double random() { return (double)rand() / RAND_MAX; }

// Material class
struct Material {
	vec3 diffuseAlbedo;	// probability of diffuse reflection
	vec3 mirrorAlbedo;	// probability of mirror like reflection

	Material(vec3 _diffuseAlbedo, vec3 _mirrorAlbedo) {
		diffuseAlbedo = _diffuseAlbedo;
		mirrorAlbedo = _mirrorAlbedo;
	}
};

// sample direction with cosine distribution, returns the pdf
double SampleDiffuse(const vec3& N, const vec3& inDir, vec3& outDir) {
	vec3 T = cross(N, vec3(1, 0, 0));	// Find a Cartesian frame T, B, N where T, B are in the plane
	if (T.Length() < epsilon) T = cross(N, vec3(0, 0, 1));
	T = T.normalize();
	vec3 B = cross(N, T);

	double x, y, z;
	do {
		x = 2 * random() - 1;    // random point in [-1,-1;1,1]
		y = 2 * random() - 1;
	} while (x*x + y * y > 1);  // reject if not in circle
	z = sqrt(1 - x * x - y * y);  // project to hemisphere

	outDir = N * z + T * x + B * y;
	return z / M_PI;	// pdf
}

// sample direction of a Dirac delta distribution, returns the pdf
double SampleMirror(const vec3& N, const vec3& inDir, vec3& outDir) {
	outDir = inDir - N * dot(N, inDir) * 2;
	return 1;	// pdf
}

// Hit of ray tracing
struct Hit {
	double t;		// ray parameter
	vec3 position;	// position of the intersection
	vec3 normal;	// normal of the intersected surface
	Material * material;	// material of the intersected surface
	Hit() { t = -1; }
};

// The ray to be traced
struct Ray {
	vec3 start, dir;
	Ray(vec3 _start, vec3 _dir) { start = _start; dir = _dir.normalize(); }
};

// Base class of objects
class Intersectable {
protected:
	Material * material;
public:
	Intersectable(Material * mat) { material = mat; }
	virtual Hit intersect(const Ray& ray) = 0;
};

// Sphere with two materials to support texturing
struct Sphere : public Intersectable {
	vec3 center;
	double radius;
	Material * material2;

	Sphere(const vec3& _center, double _radius, Material* mat1, Material* mat2 = NULL) : Intersectable(mat1) {
		center = _center;
		radius = _radius;
		material2 = mat2;
	}
	Hit intersect(const Ray& ray) {
		Hit hit;
		vec3 dist = ray.start - center;
		double b = dot(dist, ray.dir) * 2.0;
		double a = dot(ray.dir, ray.dir);
		double c = dot(dist, dist) - radius * radius;
		double discr = b * b - 4.0 * a * c;
		if (discr < 0) return hit;
		double sqrt_discr = sqrt(discr);
		double t1 = (-b + sqrt_discr) / 2.0 / a;
		double t2 = (-b - sqrt_discr) / 2.0 / a;
		if (t1 <= 0 && t2 <= 0) return hit;
		if (t1 <= 0 && t2 > 0)       hit.t = t2;
		else if (t2 <= 0 && t1 > 0)  hit.t = t1;
		else if (t1 < t2)            hit.t = t1;
		else                         hit.t = t2;
		hit.position = ray.start + ray.dir * hit.t;
		hit.normal = (hit.position - center) / radius;
		if (dot(hit.normal, ray.dir) > 0) hit.normal = hit.normal * (-1); // flip the normal, we are inside the sphere
		hit.material = material;
		if (material2) { // texturing
			double u = acos(hit.normal.y) / M_PI;
			double v = (atan2(hit.normal.z, hit.normal.x) / M_PI + 1) / 2;
			int U = (int)(u * 6), V = (int)(v * 8);
			if (U % 2 ^ V % 2) hit.material = material2;
		}
		return hit;
	}
};

// Plane
struct Plane : public Intersectable {
	vec3 point, normal;

	Plane(const vec3& _point, const vec3& _normal, Material* mat) : Intersectable(mat) {
		point = _point;
		normal = _normal.normalize();
	}
	Hit intersect(const Ray& ray) {
		Hit hit;
		double NdotV = dot(normal, ray.dir);
		if (fabs(NdotV) < epsilon) return hit;
		double t = dot(normal, point - ray.start) / NdotV;
		if (t < epsilon) return hit;
		hit.t = t;
		hit.position = ray.start + ray.dir * hit.t;
		hit.normal = normal;
		if (dot(hit.normal, ray.dir) > 0) hit.normal = hit.normal * (-1); // flip the normal, we are inside the sphere
		hit.material = material;
		return hit;
	}
};

// The virtual camera
class Camera {
	vec3 eye, lookat, right, up;
public:
	void set(vec3 _eye, vec3 _lookat, vec3 vup, double fov) {
		eye = _eye;
		lookat = _lookat;
		vec3 w = eye - lookat;
		double f = w.Length();
		right = cross(vup, w).normalize() * f * tan(fov / 2);	// orthogonalization
		up = cross(w, right).normalize() * f * tan(fov / 2);
	}
	Ray getRay(double X, double Y) {	// integer parts of X, Y define the pixel, fractional parts the point inside pixel
		vec3 dir = lookat + right * (2.0 * X / screenWidth - 1) + up * (2.0 * Y / screenHeight - 1) - eye;
		return Ray(eye, dir);
	}
};

// Point light source
struct Light {
	vec3 location;
	vec3 power;

	Light(vec3 _location, vec3 _power) {
		location = _location;
		power = _power;
	}
	double distanceOf(vec3 point) {
		return (location - point).Length();
	}
	vec3 directionOf(vec3 point) {
		return (location - point).normalize();
	}
	vec3 radianceAt(vec3 point) {
		double distance2 = dot(location - point, location - point);
		if (distance2 < epsilon) distance2 = epsilon;
		return power / distance2 / 4 / M_PI;
	}
};

// Virtual world
class Scene {
	int nObjects, nLights;
	Intersectable * objects[nMaxObjects];
	Light * lights[nMaxLights];
	Camera camera;
public:
	Scene() { nObjects = nLights = 0; }

	void build() {
		vec3 eye = vec3(0, 0, 2);
		vec3 vup = vec3(0, 1, 0);
		vec3 lookat = vec3(0, 0, 0);
		double fov = 70 * M_PI / 180;
		camera.set(eye, lookat, vup, fov);

		lights[nLights++] = new Light(vec3(2, 2, 3), vec3(500, 500, 500));

		objects[nObjects++] = new Sphere(vec3(0, 0.7, 0), 0.5, new Material(vec3(0.0, 0.0, 0.0), vec3(0.4, 0.6, 0.8)));
		objects[nObjects++] = new Sphere(vec3(0.7, 0, 0), 0.5, new Material(vec3(0.0, 0.0, 0.0), vec3(0.8, 0.6, 0.4)));
		objects[nObjects++] = new Sphere(vec3(-0.7, 0, 0), 0.5, new Material(vec3(0.6, 0.6, 0.6), vec3(0.0, 0.0, 0.0)));
		objects[nObjects++] = new Plane(vec3(0, -0.5, 0), vec3(0, 1, 0), new Material(vec3(0, 0.8, 0), vec3(0.0, 0.0, 0.0)));
		objects[nObjects++] = new Sphere(vec3(0, 0, 0), 5.0, new Material(vec3(0.3, 0.4, 0.9), vec3(0.0, 0.0, 0.0)),
			new Material(vec3(0.9, 0.4, 0.3), vec3(0.0, 0.0, 0.0)));
	}

	// Find the first intersection of the ray with objects
	Hit firstIntersect(Ray ray) {
		Hit bestHit;
		for (int iObject = 0; iObject < nObjects; iObject++) {
			Hit hit = objects[iObject]->intersect(ray); //  hit.t < 0 if no intersection
			if (hit.t > 0 && (bestHit.t < 0 || hit.t < bestHit.t)) bestHit = hit;
		}
		return bestHit;
	}

	// Trace a ray and return the radiance of the visible surface
	vec3 trace(Ray ray, int depth = 0) {
		Hit hit = firstIntersect(ray);	// Find visible surface
		vec3 outRad(0, 0, 0);
		if (hit.t < 0 || depth >= maxdepth) return outRad;	// If there is no intersection

		vec3 N = hit.normal;	// normal of the visible surface
		vec3 outDir;
		for (int iLight = 0; iLight < nLights; iLight++) {	// Direct light source computation
			outDir = lights[iLight]->directionOf(hit.position);
			Hit shadowHit = firstIntersect(Ray(hit.position + N * epsilon, outDir));
			if (shadowHit.t < epsilon || shadowHit.t > lights[iLight]->distanceOf(hit.position)) {	// if not in shadow
				double cosThetaL = dot(N, outDir);
				if (cosThetaL >= epsilon) {
					outRad += hit.material->diffuseAlbedo / M_PI * cosThetaL * lights[iLight]->radianceAt(hit.position);
				}
			}
		}

		double diffuseSelectProb = hit.material->diffuseAlbedo.average();
		double mirrorSelectProb = hit.material->mirrorAlbedo.average();

		double rnd = random();	// Russian roulette to find diffuse, mirror or no reflection
		if (rnd < diffuseSelectProb) { // diffuse
			double pdf = SampleDiffuse(N, ray.dir, outDir);
			double cosThetaL = dot(N, outDir);
			if (cosThetaL >= epsilon) {
				outRad += trace(Ray(hit.position + N * epsilon, outDir), depth + 1) * hit.material->diffuseAlbedo / M_PI * cosThetaL / pdf / diffuseSelectProb;
			}
		}
		else if (rnd < diffuseSelectProb + mirrorSelectProb) { // mirror
			double pdf = SampleMirror(N, ray.dir, outDir);
			outRad += trace(Ray(hit.position + N * epsilon, outDir), depth + 1) * hit.material->mirrorAlbedo / pdf / mirrorSelectProb;
		}
		return outRad;
	}

	// Render the scene: Trace nSamples rays through each pixel and average radiance values
	void render(vec3 image[]) {
		for (int Y = 0; Y < screenHeight; Y++) {
			printf("%d\r", Y);
#pragma omp parallel for
			for (int X = 0; X < screenWidth; X++) {
				image[Y * screenWidth + X] = vec3(0, 0, 0);
				for (int i = 0; i < nSamples; i++)
					image[Y * screenWidth + X] += trace(camera.getRay(X + random(), Y + random())) / nSamples;
			}
		}
	}
};

// Save image into a Targa format file
void SaveTGAFile(char * fileName, vec3 image[]) {
	FILE * tgaFile = fopen(fileName, "wb");
	if (!tgaFile) {
		printf("File %s cannot be opened\n", fileName);
		return;
	}
	// File header
	fputc(0, tgaFile); fputc(0, tgaFile); fputc(2, tgaFile);
	for (int i = 3; i < 12; i++) { fputc(0, tgaFile); }
	fputc(screenWidth % 256, tgaFile); fputc(screenWidth / 256, tgaFile);
	fputc(screenHeight % 256, tgaFile); fputc(screenHeight / 256, tgaFile);
	fputc(24, tgaFile); fputc(32, tgaFile);
	// List of pixel colors
	for (int Y = screenHeight - 1; Y >= 0; Y--) {
		for (int X = 0; X < screenWidth; X++) {
			int R = (int)fmax(fmin(image[Y * screenWidth + X].x * 255.5, 255.5), 0);
			int G = (int)fmax(fmin(image[Y * screenWidth + X].y * 255.5, 255.5), 0);
			int B = (int)fmax(fmin(image[Y * screenWidth + X].z * 255.5, 255.5), 0);
			fputc(B, tgaFile); fputc(G, tgaFile); fputc(R, tgaFile);
		}
	}
	fclose(tgaFile);
}

int main(int argc, char * argv[]) {
	vec3 * image = new vec3[screenWidth * screenHeight];	// create image
	Scene scene;											// create scene
	scene.build();											// define the scene
	scene.render(image);									// render the scene
	SaveTGAFile("image.tga", image);						// write out targe image file
	delete image;
}