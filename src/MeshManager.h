#pragma once

#include "Globals.h"
#define MAXPATCHMESHES 20

class MeshManager
{
public:
	Mesh SphereMesh = { 0 };
	Mesh CylinderMesh = { 0 };
	Mesh HemisphereMesh = { 0 };
	Mesh CubeMesh = { 0 };
	Mesh ConeMesh = { 0 };
	Mesh PolygonMeshes[20];
	static MeshManager& getInstance()
	{
		// The only instance
		// Guaranteed to be lazy initialized
		// Guaranteed that it will be destroyed correctly
		static MeshManager instance;
		return instance;
	}

	void InitBasicMeshes();
	void AddPoint(Mesh* m, Vector3 p, Vector3 normal, Vector2 uv, int index);
	void AddCylinderPoint(float* vs, float* ns, int* counter, float phi, float z);
	void AddSpherePoint(float* vs, float* ns, int* counter, float phi, float theta);
	void AddFlatTriangle(Mesh* m, int* index, Vector3 p1, Vector3 p2, Vector3 p3, int boundary0, int boundary1, int boundary2);
	Mesh* GetPatchMesh(float delta, bool regenerate);
	Mesh* GetBipyramidMesh(float tipsize);
	void ReloadMeshes();


private:
	Mesh GenMeshBipyramid(int sides);
	Mesh GenMeshPatch(float delta);
	Mesh GenMeshTruncatedBipyramid(int sides, float tipsize);
	Mesh GenMeshOpenCylinder(int sides);
	Mesh GenMeshPolygon(int sides);
	Mesh GenMeshCone(int slices);
	Mesh GenMeshHemisphere();


	// Private Constructor
	MeshManager();



};

