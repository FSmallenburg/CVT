#include "MeshManager.h"

extern int detail;

MeshManager::MeshManager()
{
    int i;
    for (i = 3; i < 20; i++)
    {
        PolygonMeshes[i] = GenMeshPolygon(i);
    }
    CubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
}

/***********************************************************
**                         ADDPOINT
** Add a point to a mesh
***********************************************************/
void MeshManager::AddPoint(Mesh* m, Vector3 p, Vector3 normal, Vector2 uv, int index)
{
    m->vertices[3 * index + 0] = p.x;
    m->normals[3 * index + 0] = normal.x;
    m->texcoords[2 * index + 0] = uv.x;
    m->vertices[3 * index + 1] = p.y;
    m->normals[3 * index + 1] = normal.y;
    m->texcoords[2 * index + 1] = uv.y;
    m->vertices[3 * index + 2] = p.z;
    m->normals[3 * index + 2] = normal.z;
}

/***********************************************************
**                         ADDCYLINDERPOINT
** Add a point to a mesh with a polar normal
** (Distance 1 from z-axis)
***********************************************************/
void MeshManager::AddCylinderPoint(float* vs, float* ns, int* counter, float phi, float z)
{
    float cosphi = cosf(phi), sinphi = sinf(phi);
    vs[*counter] = cosphi;
    ns[*counter] = cosphi;
    (*counter)++;
    vs[*counter] = sinphi;
    ns[*counter] = sinphi;
    (*counter)++;
    vs[*counter] = z;
    ns[*counter] = 0;
    (*counter)++;
}

/***********************************************************
**                         ADDSPHEREPOINT
** Add a point to a mesh with a radial normal
** (Distance 1 from origin)
***********************************************************/
void MeshManager::AddSpherePoint(float* vs, float* ns, int* counter, float phi, float theta)
{
    float cosphi = cosf(phi), sinphi = sinf(phi);
    float costheta = cosf(theta), sintheta = sinf(theta);
    vs[*counter] = sintheta * cosphi;
    ns[*counter] = sintheta * cosphi;
    (*counter)++;
    vs[*counter] = sintheta * sinphi;
    ns[*counter] = sintheta * sinphi;
    (*counter)++;
    vs[*counter] = costheta;
    ns[*counter] = costheta;
    (*counter)++;
}

/***********************************************************
**                         ADDFLATTRIANGLE
** Add a triangle to a mesh with a perpendicular normal
***********************************************************/
void MeshManager::AddFlatTriangle(Mesh* m, int* index, Vector3 p1, Vector3 p2, Vector3 p3, int boundary0, int boundary1, int boundary2)
{
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p2, p1), Vector3Subtract(p3, p1)));
    int packedboundaries = (1 - boundary0) * 1 + (1 - boundary1) * 2 + (1 - boundary2) * 4;
    AddPoint(m, p1, normal, { (float)(packedboundaries | 1), 0 }, (*index)++);
    AddPoint(m, p2, normal, { (float)(packedboundaries | 2), 0 }, (*index)++);
    AddPoint(m, p3, normal, { (float)(packedboundaries | 4), 0 }, (*index)++);
}

/***********************************************************
**                         GETPATCHMESH
** Get a patch mesh with a specific opening angle
** Stores past ones for reuse.
***********************************************************/
Mesh* MeshManager::GetPatchMesh(float delta, bool regenerate)
{
    static Mesh stored[MAXPATCHMESHES];
    static int nstored = 0;
    static int lastchanged = 0;
    static float deltas[MAXPATCHMESHES];

    if (regenerate)
    {
        for (int i = 0; i < nstored; i++)
        {
            UnloadMesh(stored[i]);
            stored[i] = GenMeshPatch(deltas[i]);

        }
        return NULL;
    }



    if (delta < 0)      //Clean up the buffer.
    {
        for (int i = 0; i < nstored; i++)
        {
            UnloadMesh(stored[i]);
        }
        nstored = 0;
        return NULL;
    }

    for (int i = 0; i < nstored; i++)
    {
        if (deltas[i] == delta) return &(stored[i]);
    }

    if (nstored < MAXPATCHMESHES)
    {
        stored[nstored] = GenMeshPatch(delta);
        deltas[nstored] = delta;
        lastchanged = nstored;
        nstored++;
        return &(stored[lastchanged]);
    }
    else
    {
        lastchanged = (lastchanged + 1) % MAXPATCHMESHES;
        UnloadMesh(stored[lastchanged]);
        stored[lastchanged] = GenMeshPatch(delta);
        deltas[lastchanged] = delta;
        return &(stored[lastchanged]);
    }

}



/***********************************************************
**                   GETBIPYRAMIDMESH
** Get a bipyramid mesh with a specific opening tip size
***********************************************************/
Mesh* MeshManager::GetBipyramidMesh(float tipsize)
{
    static Mesh stored[MAXPATCHMESHES];
    static int nstored = 0;
    static int lastchanged = 0;
    static float tipsizes[MAXPATCHMESHES];


    if (tipsize < 0)      //Clean up the buffer.
    {
        for (int i = 0; i < nstored; i++)
        {
            UnloadMesh(stored[i]);
        }
        nstored = 0;
        return &(stored[0]);
    }

    for (int i = 0; i < nstored; i++)
    {
        if (tipsizes[i] == tipsize) return &(stored[i]);
    }

    if (nstored < MAXPATCHMESHES)
    {
        if (tipsize == 0) stored[nstored] = GenMeshBipyramid(5);
        else              stored[nstored] = GenMeshTruncatedBipyramid(5, tipsize);
        tipsizes[nstored] = tipsize;
        lastchanged = nstored;
        nstored++;
        return &(stored[lastchanged]);
    }
    else
    {
        lastchanged = (lastchanged + 1) % MAXPATCHMESHES;
        UnloadMesh(stored[lastchanged]);
        if (tipsize == 0) stored[lastchanged] = GenMeshBipyramid(5);
        else              stored[lastchanged] = GenMeshTruncatedBipyramid(5, tipsize);
        tipsizes[lastchanged] = tipsize;
        return &(stored[lastchanged]);
    }

}

/***********************************************************
**                      GENMESHCONE
** Generate a cone mesh with the tip at the origin,
** and the cone symmetric around the z-axis.
***********************************************************/
Mesh MeshManager::GenMeshCone(int slices)
{
    Mesh mesh = { 0 };
    int i;

    float dphi = 2 * PI / slices;
    float invsq2 = 1.0/sqrtf(2.0f);

    int counter = 0;

    mesh.vertexCount = slices * 6;
    mesh.triangleCount = slices *2;


    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));


    for (i = 0; i < slices; i++)        //Cone part
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = cosf(0.5f * phi1 + 0.5f * phi2) * invsq2;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = sinf(0.5f * phi1 + 0.5f * phi2) * invsq2;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = invsq2;

        mesh.vertices[counter] = cosf(phi2);
        mesh.normals[counter++] = cosf(phi2) * invsq2;
        mesh.vertices[counter] = sinf(phi2);
        mesh.normals[counter++] = sinf(phi2) * invsq2;
        mesh.vertices[counter] = 1;
        mesh.normals[counter++] = invsq2;

        mesh.vertices[counter] = cosf(phi1);
        mesh.normals[counter++] = cosf(phi1) * invsq2;
        mesh.vertices[counter] = sinf(phi1);
        mesh.normals[counter++] = sinf(phi1) * invsq2;
        mesh.vertices[counter] = 1;
        mesh.normals[counter++] = invsq2;
    }

    for (i = 0; i < slices; i++)        //Cone part
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = 1;
        mesh.normals[counter++] = 1;

        mesh.vertices[counter] = cosf(phi1);
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = sinf(phi1);
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = 1;
        mesh.normals[counter++] = 1;

        mesh.vertices[counter] = cosf(phi2);
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = sinf(phi2);
        mesh.normals[counter++] = 0;
        mesh.vertices[counter] = 1;
        mesh.normals[counter++] = 1;
    }    


    


    // for (i = 0; i < 3 * mesh.vertexCount; i++)
    // {
    //     mesh.vertices[i] *= 0.5f;
    // }
    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}

/***********************************************************
**                      GENMESHPATCH
** Generate a patch mesh with a specific opening angle.
** delta = cos theta
***********************************************************/
Mesh MeshManager::GenMeshPatch(float delta)
{
    Mesh mesh = { 0 };
    int rings = 2 + detail;
    int slices = 8 + 2 * detail;

    int i, j;

    float dphi = 2 * PI / slices;
    float thetamax = acosf(delta);
    if (thetamax < 1) rings = 3;
    float dtheta = thetamax / (rings);

    int counter = 0;

    mesh.vertexCount = slices * 3 + slices * rings * 6 + slices * 3;
    mesh.triangleCount = slices + slices * rings * 2 + slices;


    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));


    for (i = 0; i < slices; i++)        //Cone part
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = cosf(0.5f * phi1 + 0.5f * phi2) * cosf(thetamax);
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = sinf(0.5f * phi1 + 0.5f * phi2) * cosf(thetamax);
        mesh.vertices[counter] = 0;
        mesh.normals[counter++] = sinf(thetamax);

        mesh.vertices[counter] = sinf(thetamax) * cosf(phi2);
        mesh.normals[counter++] = cosf(phi2) * cosf(thetamax);
        mesh.vertices[counter] = sinf(thetamax) * sinf(phi2);
        mesh.normals[counter++] = sinf(phi2) * cosf(thetamax);
        mesh.vertices[counter] = cosf(thetamax);
        mesh.normals[counter++] = sinf(thetamax);

        mesh.vertices[counter] = sinf(thetamax) * cosf(phi1);
        mesh.normals[counter++] = cosf(phi1) * cosf(thetamax);
        mesh.vertices[counter] = sinf(thetamax) * sinf(phi1);
        mesh.normals[counter++] = sinf(phi1) * cosf(thetamax);
        mesh.vertices[counter] = cosf(thetamax);
        mesh.normals[counter++] = sinf(thetamax);
    }


    for (i = 0; i < slices; i++)
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        for (j = 0; j < rings; j++)
        {
            float theta1 = j * dtheta;
            float theta2 = theta1 + dtheta;

            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta1);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta2);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta2);

            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta1);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta2);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta1);
        }

        AddSpherePoint(mesh.vertices, mesh.normals, &counter, 0, 0);
        AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, dtheta);
        AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, dtheta);
    }



    for (i = 0; i < 3 * mesh.vertexCount; i++)
    {
        mesh.vertices[i] *= 0.5f;
    }
    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}

/***********************************************************
**                      GENMESHHEMISPHERE
** Generate a hemisphere
***********************************************************/
Mesh MeshManager::GenMeshHemisphere()
{
    Mesh mesh = { 0 };
    int rings = 2 + detail;
    int slices = 8 + 2 * detail;

    int i, j;

    float dphi = 2 * PI / slices;
    float thetamax = M_PI / 2.0f;
    if (thetamax < 1) rings = 3;
    float dtheta = thetamax / (rings);

    int counter = 0;

    mesh.vertexCount = slices * rings * 6 + slices * 3;
    mesh.triangleCount = slices * rings * 2 + slices;


    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));


    for (i = 0; i < slices; i++)
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        for (j = 0; j < rings; j++)
        {
            float theta1 = j * dtheta;
            float theta2 = theta1 + dtheta;

            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta1);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta2);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta2);

            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, theta1);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta2);
            AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, theta1);
        }

        AddSpherePoint(mesh.vertices, mesh.normals, &counter, 0, 0);
        AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi1, dtheta);
        AddSpherePoint(mesh.vertices, mesh.normals, &counter, phi2, dtheta);
    }


    for (i = 0; i < 3 * mesh.vertexCount; i++)
    {
        mesh.vertices[i] *= 0.5f;
    }
    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}



/***********************************************************
**                         GENMESHBIPYRAMID
** Creates a bipyramid with n sides.
***********************************************************/
Mesh MeshManager::GenMeshBipyramid(int sides)
{
    Mesh mesh = { 0 };

    int i;

    float dphi = 2 * PI / sides;

    int index = 0;

    mesh.triangleCount = sides * 2;
    mesh.vertexCount = 3 * mesh.triangleCount;

    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));

    Vector3 tip1 = { 0, 0, 1 };
    Vector3 tip2 = { 0, 0, -1 };

    for (i = 0; i < sides; i++)
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        Vector3 p1 = { cosf(phi1), sinf(phi1), 0 };
        Vector3 p2 = { cosf(phi2), sinf(phi2), 0 };

        AddFlatTriangle(&mesh, &index, p1, p2, tip1, 1, 1, 1);
        AddFlatTriangle(&mesh, &index, p2, p1, tip2, 1, 1, 1);

    }
    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}


/***********************************************************
**                         GENMESHPOLYGON
** Creates a bipyramid with n sides.
***********************************************************/
Mesh MeshManager::GenMeshPolygon(int sides)
{
    Mesh mesh = { 0 };

    int i;

    float dphi = 2 * PI / sides;

    int index = 0;
    mesh.triangleCount = 2*sides ;
    mesh.vertexCount = 3 * mesh.triangleCount;

    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));

    Vector3 center = { 0, 0, 0 };

    for (i = 0; i < sides; i++)
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        Vector3 p1 = { cosf(phi1), sinf(phi1), 0 };
        Vector3 p2 = { cosf(phi2), sinf(phi2), 0 };

        AddFlatTriangle(&mesh, &index, p1, p2, center, 0, 0, 1);
        AddFlatTriangle(&mesh, &index, p2, p1, center, 0, 0, 1); //Add back face to allow us to see it from the back.

    }
    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}

/***********************************************************
**                         GENMESHTRUNCATEDBIPYRAMID
** Creates a bipyramid with n sides.
** d is the radius of the truncated tip (divided by the center radius).
***********************************************************/
Mesh MeshManager::GenMeshTruncatedBipyramid(int sides, float tipsize)
{
    Mesh mesh = { 0 };

    int i;

    float dphi = 2 * PI / sides;

    int counter = 0;

    mesh.triangleCount = 2 * sides + sides * 2 * 2;
    mesh.vertexCount = 3 * mesh.triangleCount;

    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));


    for (i = 0; i < sides; i++)     //Side faces
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        Vector3 p1 = { cosf(phi1), sinf(phi1), 0 };
        Vector3 p2 = { cosf(phi2), sinf(phi2), 0 };
        Vector3 p3 = { tipsize * cosf(phi1), tipsize * sinf(phi1), 1 };
        Vector3 p4 = { tipsize * cosf(phi2), tipsize * sinf(phi2), 1 };

        AddFlatTriangle(&mesh, &counter, p1, p2, p3, 0, 1, 1);
        AddFlatTriangle(&mesh, &counter, p2, p4, p3, 1, 0, 1);

        p3.z = -1;
        p4.z = -1;

        AddFlatTriangle(&mesh, &counter, p1, p3, p2, 0, 1, 1);
        AddFlatTriangle(&mesh, &counter, p2, p3, p4, 1, 1, 0);

    }

    Vector3 tip1 = { 0, 0, 1 };
    Vector3 tip2 = { 0, 0, -1 };
    for (i = 0; i < sides; i++)
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        Vector3 p1 = { tipsize * cosf(phi1), tipsize * sinf(phi1), 1 };
        Vector3 p2 = { tipsize * cosf(phi2), tipsize * sinf(phi2), 1 };

        AddFlatTriangle(&mesh, &counter, p1, p2, tip1, 0, 0, 1);

        p1.z = -1;
        p2.z = -1;
        AddFlatTriangle(&mesh, &counter, p2, p1, tip2, 0, 0, 1);

    }

    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}


/***********************************************************
**                GENMESHOPENCYLINDER
** Creates a cylinder aligned with the z-axis
***********************************************************/
Mesh MeshManager::GenMeshOpenCylinder(int sides)
{
    Mesh mesh = { 0 };

    int i;

    float dphi = 2 * PI / sides;

    int counter = 0;

    mesh.triangleCount = sides * 2;
    mesh.vertexCount = 3 * mesh.triangleCount;

    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));


    for (i = 0; i < sides; i++)     //Side faces
    {
        float phi1 = i * dphi;
        float phi2 = phi1 + dphi;
        //Vector3 p1 = { cosf(phi1), sinf(phi1), -1 };
        //Vector3 p2 = { cosf(phi2), sinf(phi2), -1 };
        //Vector3 p3 = { cosf(phi1), tipsize * sinf(phi1), 1 };
        //Vector3 p4 = { cosf(phi2), tipsize * sinf(phi2), 1 };

        //AddFlatTriangle(&mesh, &counter, p1, p2, p3);
        //AddFlatTriangle(&mesh, &counter, p2, p4, p3);


        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi1, -1);
        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi2, -1);
        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi1, 1);

        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi2, -1);
        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi2, 1);
        AddCylinderPoint(mesh.vertices, mesh.normals, &counter, phi1, 1);
    }


    for (i = 0; i < 3 * mesh.vertexCount; i++)
    {
        mesh.vertices[i] *= 0.5f;
    }

    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);

    return mesh;
}





/******************************************************
**                    RELOADMESHES
******************************************************/
void MeshManager::ReloadMeshes()
{
    if (SphereMesh.vertexCount != 0) UnloadMesh(SphereMesh);
    SphereMesh = GenMeshSphere(0.5f, 8 + detail, 8 + detail);

    if (HemisphereMesh.vertexCount != 0) UnloadMesh(HemisphereMesh);
    HemisphereMesh = GenMeshHemisphere();

    if (CylinderMesh.vertexCount != 0) UnloadMesh(CylinderMesh);
    CylinderMesh = GenMeshOpenCylinder(8 + detail);

    if (ConeMesh.vertexCount != 0) UnloadMesh(ConeMesh);
    ConeMesh = GenMeshCone(8 + detail);

    GetPatchMesh(0, true);
}


