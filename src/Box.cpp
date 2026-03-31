#include "Box.h"

Box::Box()
{
    set_rect_box(0, 1, 0, 1, 0, 1);
}


/******************************************************
**               SET_RECT_BOX
******************************************************/
void Box::set_rect_box(float xa, float xb, float ya, float yb, float za, float zb)
{
    VSET(bb[0], xa, ya, za);
    VSET(bb[1], xb, ya, za);
    VSET(bb[2], xb, yb, za);
    VSET(bb[3], xa, yb, za);

    VSET(bb[4], xa, ya, zb);
    VSET(bb[5], xb, ya, zb);
    VSET(bb[6], xb, yb, zb);
    VSET(bb[7], xa, yb, zb);
    Lx = (bb[1][0] - bb[0][0]);
    Ly = (bb[3][1] - bb[0][1]);
    Lz = (bb[4][2] - bb[0][2]);
}

/******************************************************
**               SET_FLOPPY_BOX
******************************************************/
void Box::set_floppy_box(float m[9])
{
    VSET(bb[0], 0, 0, 0);
    VSET(bb[1], m[0], m[1], m[2]);
    VSET(bb[2], m[0] + m[3], m[1] + m[4], m[2] + m[5]);
    VSET(bb[3], m[3], m[4], m[5]);

    VSET(bb[4], m[6], m[7], m[8]);
    VSET(bb[5], m[0] + m[6], m[1] + m[7], m[2] + m[8]);
    VSET(bb[6], m[0] + m[3] + m[6], m[1] + m[4] + m[7], m[2] + m[5] + m[8]);
    VSET(bb[7], m[3] + m[6], m[4] + m[7], m[5] + m[8]);
    Lx = (bb[1][0] - bb[0][0]);
    Ly = (bb[3][1] - bb[0][1]);
    Lz = (bb[4][2] - bb[0][2]);
}

/******************************************************
**               READBOX
******************************************************/
void Box::readbox(FILE* f, int filetype)
{
    char str[512];
    dimensions = 3;
    channel = 0;
    radius = 0;
    

    int boxsize_unknown = false;
    mygetline(str, f);
    float x_box_b;
    float y_box_b;
    float z_box_b;
    float m[9];

    if (filetype == 14) //channel
    {
        channel = 1;
        int r = sscanf(str, "%e %e %e   Amp: %f  L: %f  Margin: %f", &x_box_b, &y_box_b, &z_box_b, &(channelpara[0]), &(channelpara[1]), &(channelpara[2]));
        if (r < 5)
        {
            r = sscanf(str, "%e %e %e  Dist: %f Margin: %f", &x_box_b, &y_box_b, &z_box_b, &(channelpara[0]), &(channelpara[2]));
            if (r == 5)
            {
                channelpara[1] = 0;
                set_rect_box(0.0, x_box_b, 0.0, y_box_b, 0.0, z_box_b);
            }
            else printf("error reading box and channel data\n");
        }
        else
        {
            set_rect_box(0.0, x_box_b, 0.0, y_box_b, 0.0, z_box_b);
            if (r < 6) channelpara[2] = 0;
        }
        return;
    }

    if (sscanf(str, "%f %f %f %f %f %f %f %f %f", m + 0, m + 1, m + 2, m + 3, m + 4, m + 5, m + 6, m + 7, m + 8) == 9)
    {
        set_floppy_box(m);
        putparticlesbackinbox = 0;
    }
    else
    {
        int r = sscanf(str, "%e %e %e", &x_box_b, &y_box_b, &z_box_b);
        if (r < 2)
        {
            float radnow;
            int radii = sscanf(str, "ball %e %e", &radius, &radnow);

            if (radii >= 1)
            {
                x_box_b = radius;
                y_box_b = radius;
                z_box_b = radius;
                set_rect_box(-x_box_b, x_box_b, -y_box_b, y_box_b, -z_box_b, z_box_b);
                if (radii == 2) radius = radnow;
            }
            else
            {
                x_box_b = 20;
                y_box_b = 20;
                z_box_b = 20;
                boxsize_unknown = true;
                boundingbox = 0;
            }
            periodicboundaries = 0;
            putparticlesbackinbox = 0;
        }
        else
        {
            if (r == 2)
            {
                dimensions = 2;
                z_box_b = 1;
            }
            char* ptr = strstr(str, "Offset:");     //Check for offsets
            if (ptr)			//Box offsets given
            {
                ptr = ptr + 7;
                int shiftread = sscanf(ptr, "%f %f %f", &shiftxy, &shiftzx, &shiftzy);
                if (dimensions == 3 && shiftread != 3)
                {
                    printf("Strange offsets!\n%s\n", ptr);
                    shiftxy = 0;
                    shiftzx = 0;
                    shiftzy = 0;
                }
                else if (dimensions == 2)
                {
                    shiftzx = 0;
                    shiftzy = 0;
                    if (shiftread < 1)
                    {
                        printf("Strange offsets!\n%s\n", ptr);
                    }
                }

            }
            else
            {
                shiftxy = 0;
                shiftzx = 0;
                shiftzy = 0;
            }
            set_rect_box(0.0, x_box_b, 0.0, y_box_b, 0.0, z_box_b);
        }
    }
    UpdateBoxTransform();

}

/***************************************************************************
**                 FIXUNKNOWNBOX
****************************************************************************/
void Box::FixUnknownBox()
{
    float maxx = 0, maxy = 0, maxz = 0, maxr = 0;
    float minx = 0, miny = 0, minz = 0;
    for (int i = 0; i < n_part; i++)
    {
        if (particles[i]->pos.x > maxx) maxx = particles[i]->pos.x; 
        if (particles[i]->pos.y > maxy) maxy = particles[i]->pos.y;  
        if (particles[i]->pos.z > maxz) maxz = particles[i]->pos.z;
        if (particles[i]->pos.x < minx) minx = particles[i]->pos.x; 
        if (particles[i]->pos.y < miny) miny = particles[i]->pos.y;  
        if (particles[i]->pos.z < minz) minz = particles[i]->pos.z;
        if (particles[i]->size1 > maxr) maxr = particles[i]->size1;
    }
    maxx += maxr; maxy += maxr; maxz += maxr;
    set_rect_box(minx, maxx, miny, maxy, minz, maxz);
    boxsize_unknown = false;
}



/***************************************************************************
**                 UPDATEBOXTRANSFORM
****************************************************************************/
void Box::UpdateBoxTransform()
{

    int i, j;
    //Set the scale factor
    float s;
    float v[3];
    VSUB(v, bb[0], bb[1]);
    s = VLEN(v);
    VSUB(v, bb[0], bb[3]);
    if (VLEN(v) > s) s = VLEN(v);
    VSUB(v, bb[0], bb[4]);
    if (VLEN(v) > s) s = VLEN(v);
    s = 2.0f / s;

    scalefac = s;


    //Calculate center
    for (i = 0; i < 3; i++) 
    {
        v[i] = 0;
        for (j = 0; j < 8; j++) v[i] -= bb[j][i] / 8.0f;
    }


    transform = MatrixMultiply(MatrixTranslate(v[0], v[1], v[2]), MatrixMultiply(MatrixUniformScale(s), rot_matrix));
}

/**********************************************************
** 		       BACKINBOX
** Applies periodic boundaries
** Each component ends up in [0,L_{xyz}]
** Assumes rectangular box, possibly with shifted boundaries
** TODO: floppy box?
**********************************************************/
void Box::backinbox(float* x, float* y, float* z)
{
    int nz = (int)floorf(*z / Lz);
    *x -= shiftzx * nz * Lx;
    *y -= shiftzy * nz * Ly;
    *z -= nz * Lz;
    int nx = (int)floorf(*x / Lx);
    *y -= shiftxy * nx * Ly;
    *x -= nx * Lx;
    int ny = (int)floorf(*y / Ly);
    *y -= ny * Ly;
}

/**********************************************************
** 		       NEARESTIMAGE
** Applies periodic boundaries
** Each component ends up in [-L/2,L/2]
** Assumes rectangular box, possibly with shifted boundaries
** TODO: floppy box?
**********************************************************/
void Box::nearestimage(float* x, float* y, float* z)
{
    int nz = (int)rint(*z / Lz);
    *x -= shiftzx * nz * Lx;
    *y -= shiftzy * nz * Ly;
    *z -= nz * Lz;
    int nx = (int)rint(*x / Lx);
    *y -= shiftxy * nx * Ly;
    *x -= nx * Lx;
    int ny = (int)rint(*y / Ly);
    *y -= ny * Ly;
}

Vector3 Box::NearestImage(Vector3 v)
{
    float x = v.x;
    float y = v.y;
    float z = v.z;
    nearestimage(&x, &y, &z);
    Vector3 out = {x,y,z};
    return out;
}



/**********************************************************
** 		       RENDER
**********************************************************/
void Box::Render()
{
    Color linecolor = { 50, 50, 50, 255 };     // Color of the box
    if (radius == 0)
    {
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(box.transform));
        DrawLine3D(ToVector3(bb[0]), ToVector3(bb[1]), linecolor);
        DrawLine3D(ToVector3(bb[1]), ToVector3(bb[2]), linecolor);
        DrawLine3D(ToVector3(bb[2]), ToVector3(bb[3]), linecolor);
        DrawLine3D(ToVector3(bb[3]), ToVector3(bb[0]), linecolor);

        DrawLine3D(ToVector3(bb[4]), ToVector3(bb[5]), linecolor);
        DrawLine3D(ToVector3(bb[5]), ToVector3(bb[6]), linecolor);
        DrawLine3D(ToVector3(bb[6]), ToVector3(bb[7]), linecolor);
        DrawLine3D(ToVector3(bb[7]), ToVector3(bb[4]), linecolor);

        DrawLine3D(ToVector3(bb[0]), ToVector3(bb[4]), linecolor);
        DrawLine3D(ToVector3(bb[1]), ToVector3(bb[5]), linecolor);
        DrawLine3D(ToVector3(bb[2]), ToVector3(bb[6]), linecolor);
        DrawLine3D(ToVector3(bb[3]), ToVector3(bb[7]), linecolor);
        rlPopMatrix();
    }
    else
    {
        Vector3 center = { 0,0,0 };
        Vector3 zaxis = { 0,0,1 };
        DrawCircle3D(center, scalefac*radius , zaxis, 0, linecolor); //TODO: render in front of scene
    }

    if (channel)
    {
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(box.transform));
        float amp = channelpara[0];
        float len = channelpara[1];
        float mar = channelpara[2];
        if (len != 0)		//Constriction
        {
            int i;
            int npoints = 10 * detail;
            float spacing = len / (2 * npoints);
            float cosfac = 2 * M_PI / len * spacing;

            linecolor = RED;
            Vector3 lastpt = { 0, mar , 0 };
            Vector3 newpt;
            for (i = -npoints; i <= npoints; i++)
            {
                newpt = { spacing * i + Lx / 2, mar + 0.5f * amp * (1 + cosf(cosfac * i)), 0 };
                DrawLine3D(lastpt, newpt, linecolor);
                lastpt = newpt;
            }
            newpt = { Lx , mar , 0 };
            DrawLine3D(lastpt, newpt, linecolor);

            lastpt = { 0, -mar + Ly, 0 };
            for (i = -npoints; i <= npoints; i++)
            {
                newpt = { Lx / 2 + spacing * i, -mar + Ly - 0.5f * amp * (1 + cosf(cosfac * i)), 0 };
                DrawLine3D(lastpt, newpt, linecolor);
                lastpt = newpt;
            }
            newpt = { Lx, -mar + Ly, 0 };
            DrawLine3D(lastpt, newpt, linecolor);
        }
        else			//Barrier //TODO
        {
            //int i;
            //int npoints = 2;
            //double spacing = len / (2 * npoints);

            //glScalef(1 / sizescale, 1 / sizescale, 1 / sizescale);

            //glColor4f(0.4, 1.0, 0.4, 1.0);
            //glBegin(GL_QUADS);
            //glVertex3f(-0.5, -mar + Ly / 2, 0);
            //glVertex3f(-0.5, mar - Ly / 2, 0);
            //glVertex3f(0.5, mar - Ly / 2, 0);
            //glVertex3f(0.5, -mar + Ly / 2, 0);
            //glEnd();
            //glBegin(GL_QUADS);
            //glVertex3f(-0.5 + amp, -mar + Ly / 2, 0);
            //glVertex3f(-0.5 + amp, mar - Ly / 2, 0);
            //glVertex3f(0.5 + amp, mar - Ly / 2, 0);
            //glVertex3f(0.5 + amp, -mar + Ly / 2, 0);
            //glEnd();

            //glColor4f(0.0, 0.5, 0.0, 1.0);
            //glBegin(GL_LINE_STRIP);
            //glVertex3f(-Lx / 2, mar - Ly / 2, 0);
            //for (i = -npoints; i <= npoints; i++)
            //{
            //    glVertex3f(spacing * i, mar - Ly / 2, 0);
            //}
            //glVertex3f(Lx / 2, mar - Ly / 2, 0);
            //glEnd();

            //glBegin(GL_LINE_STRIP);
            //glVertex3f(-Lx / 2, -mar + Ly / 2, 0);
            //for (i = -npoints; i <= npoints; i++)
            //{
            //    glVertex3f(spacing * i, -mar + Ly / 2, 0);
            //}
            //glVertex3f(Lx / 2, -mar + Ly / 2, 0);
            //glEnd();


            //glPopMatrix();
        }
        rlPopMatrix();
    }

    //glLineWidth(fCurrSize);
}