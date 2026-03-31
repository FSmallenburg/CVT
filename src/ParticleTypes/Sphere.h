#pragma once
#include "Particle.h"
#include "Globals.h"
#include "HelperFunctions.h"
#include "MeshManager.h"
#include "Box.h"

class Sphere : public Particle
{
public: 



    Sphere()
    {
        particletype = 1;
        mesh = sphereMesh;
        material = defaultMaterial;
        nsizes = 1;
    }


    /*************************************************************
    ** 	          RENDER
    *************************************************************/
    // void Render()
    // {    
    //     if (detail == 11)
    //     {
    //         Transform = MatrixMultiply(MatrixUniformScale(2*size1 * scale), MatrixMultiply(matrix, MatrixTranslate(pos.x, pos.y, pos.z)));
    //         DrawParticleMesh(cubeMesh, Transform, GetDrawColor(), testMaterial);
    //     }
    //     else
    //     {
    //         Transform = MatrixMultiply(MatrixUniformScale(2*size1 * scale), MatrixMultiply(matrix, MatrixTranslate(pos.x, pos.y, pos.z)));
    //         DrawParticleMesh(mesh, Transform, GetDrawColor(), material);
    //     }
    // }



};

