#define MAXBONDS 12
int patchy2d = 0;

void placepatches()
{    
    static double patches[MAXBONDS + 1][MAXBONDS + 1][3];
    int i,j,k;
    if (patchy2d)
    {
      for (i = 1; i < 12; i++)
      {
        double ang = M_PI * 2 / i;
        for (j = 0; j < i; j++)
        {
          patches[i][j][0] = cos(j * ang);
          patches[i][j][1] = sin(j * ang);
          patches[i][j][2] = 0;
        }
      }
    }
    else
    {
      double fac = (1.0 + sqrt(5.0)) * 0.5;
      double a = sqrt(1 + fac * fac);
      double b = 1 / a;
      a = fac / a;
      k = 0;
      for (i = -1; i < 2; i += 2)
        for (j = -1; j < 2; j += 2)
        {
          patches[12][k][0] = i * b;
          patches[12][k][1] = 0;
          patches[12][k][2] = j * a;
          k++;
        }
      for (i = -1; i < 2; i += 2)
        for (j = -1; j < 2; j += 2)
        {
          patches[12][k][0] = 0;
          patches[12][k][1] = i * a;
          patches[12][k][2] = j * b;
          k++;
        }
      for (i = -1; i < 2; i += 2)
        for (j = -1; j < 2; j += 2)
        {
          patches[12][k][0] = i * a;
          patches[12][k][1] = j * b;
          patches[12][k][2] = 0;
          k++;
        }
    }
      double fac  = 0.511081;
      double fac2 = 0.607781;
      double fac3 = 0.859533;

      patches[8][0][0] = -fac2;
      patches[8][0][1] = -fac2;
      patches[8][0][2] = -fac;
      patches[8][1][0] = -fac2;
      patches[8][1][1] = fac2;
      patches[8][1][2] = -fac;
      patches[8][2][0] = 0;
      patches[8][2][1] = -fac3;
      patches[8][2][2] = fac;
      patches[8][3][0] = 0;
      patches[8][3][1] = fac3;
      patches[8][3][2] = fac;
      patches[8][4][0] = fac2;
      patches[8][4][1] = -fac2;
      patches[8][4][2] = -fac;
      patches[8][5][0] = fac2;
      patches[8][5][1] = fac2;
      patches[8][5][2] = -fac;
      patches[8][6][0] = -fac3;
      patches[8][6][1] = 0;
      patches[8][6][2] = fac;
      patches[8][7][0] = fac3;
      patches[8][7][1] = 0;
      patches[8][7][2] = fac;

      patches[6][0][0] = 1;
      patches[6][0][1] = 0;
      patches[6][0][2] = 0;
      patches[6][3][0] = -1;
      patches[6][3][1] = 0;
      patches[6][3][2] = 0;
      patches[6][1][0] = 0;
      patches[6][1][1] = 1;
      patches[6][1][2] = 0;
      patches[6][4][0] = 0;
      patches[6][4][1] = -1;
      patches[6][4][2] = 0;
      patches[6][2][0] = 0;
      patches[6][2][1] = 0;
      patches[6][2][2] = 1;
      patches[6][5][0] = 0;
      patches[6][5][1] = 0;
      patches[6][5][2] = -1;

      fac = sqrt(3) / 2.0;
      patches[5][0][0] = 0;
      patches[5][0][1] = 0;
      patches[5][0][2] = 1;
      patches[5][1][0] = 0;
      patches[5][1][1] = 0;
      patches[5][1][2] = -1;
      patches[5][2][0] = 1;
      patches[5][2][1] = 0;
      patches[5][2][2] = 0;
      patches[5][3][0] = -0.5;
      patches[5][3][1] = fac;
      patches[5][3][2] = 0;
      patches[5][4][0] = -0.5;
      patches[5][4][1] = -fac;
      patches[5][4][2] = 0;
     fac = 1.0 / sqrt(3);
      patches[4][0][0] = fac;
      patches[4][0][1] = fac;
      patches[4][0][2] = fac;
      patches[4][1][0] = -fac;
      patches[4][1][1] = -fac;
      patches[4][1][2] = fac;
      patches[4][2][0] = fac;
      patches[4][2][1] = -fac;
      patches[4][2][2] = -fac;
      patches[4][3][0] = -fac;
      patches[4][3][1] = fac;
      patches[4][3][2] = -fac;
      fac = sqrt(3) / 2.0;
      patches[3][0][0] = 0;
      patches[3][0][1] = 0;
      patches[3][0][2] = 1;
      patches[3][1][0] = fac;
      patches[3][1][1] = 0;
      patches[3][1][2] = -0.5;
      patches[3][2][0] = -fac;
      patches[3][2][1] = 0;
      patches[3][2][2] = -0.5;
      patches[2][0][0] = 0;
      patches[2][0][1] = 0;
      patches[2][0][2] = 1;
      patches[2][1][0] = 0;
      patches[2][1][1] = 0;
      patches[2][1][2] = -1;
      patches[1][0][0] = 0;
      patches[1][0][1] = 0;
      patches[1][0][2] = 1;

}
