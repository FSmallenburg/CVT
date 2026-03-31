#pragma once

#include "Globals.h"
#include "Particle.h"
#include <stdio.h>
#include <string.h>

class Box
{
public:
	Box();
	float bb[8][3];
	float shiftxy = 0;		//Shifted boundary conditions
	float shiftzx = 0;
	float shiftzy = 0;
	float radius;
	float Lx, Ly, Lz;
	bool boxsize_unknown;
	Matrix transform;
	int dimensions = 3;

	int channel = 0;
	float channelpara[3] = { 0,0,0 };


	void set_rect_box(float xa, float xb, float ya, float yb, float za, float zb);
	void set_floppy_box(float m[9]);
	void FixUnknownBox();


	void readbox(FILE* f, int filetype);
	void UpdateBoxTransform();
	void backinbox(float* x, float* y, float* z);
	void nearestimage(float* x, float* y, float* z);
	Vector3 NearestImage(Vector3 v);

	void Render();

	float scalefac = 0;
};

