#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Constantes físicas (ISO 10077-2)
#define Rse  0.04
#define Rsi  0.13
#define SIGMA 5.67e-8
#define Tm   283.0
#define epsilon 0.9   // se pasa desde JS

typedef struct { double x, y; } Point;
typedef struct { Point p1, p2; uint8_t type; } BoundaryEdge;
typedef struct { double k; } Material;

// Cabeceras de las funciones exportadas
void solve_thermal(
    // Entradas
    double* polygons_flat, int* polygons_lengths, int num_polygons,
    double* boundary_edges_flat, int num_boundary_edges,
    double calcBounds_minX, double calcBounds_maxX, double calcBounds_minY, double calcBounds_maxY,
    double* materials_k, int num_materials,
    double Lp, double tExt, double tInt, double emiss, double gridRes,
    // Salidas
    double** T_out, double** kGrid_out, int* resX_out, int* resY_out,
    double* lastTsiMinX, double* lastTsiMinY, double* lastL2D, double* lastU,
    double* lastFlux, double* lastFRsi, double* lastTsiMin
);

// Función auxiliar: test punto en polígono (solo para scanline, ya no se usa per‑pixel)
static int point_in_poly(const Point* pts, int n, double x, double y) {
    int inside = 0;
    for (int i = 0, j = n-1; i < n; j = i++) {
        if ((pts[i].y > y) != (pts[j].y > y)) {
            double xint = (pts[j].x - pts[i].x) * (y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x;
            if (x < xint) inside = !inside;
        }
    }
    return inside;
}

void solve_thermal(
    double* polygons_flat, int* polygons_lengths, int num_polygons,
    double* boundary_edges_flat, int num_boundary_edges,
    double calcBounds_minX, double calcBounds_maxX, double calcBounds_minY, double calcBounds_maxY,
    double* materials_k, int num_materials,
    double Lp, double tExt, double tInt, double emiss, double gridRes,
    double** T_out, double** kGrid_out, int* resX_out, int* resY_out,
    double* lastTsiMinX, double* lastTsiMinY, double* lastL2D, double* lastU,
    double* lastFlux, double* lastFRsi, double* lastTsiMin
) {
    // 1. Dimensiones de la malla
    double w = calcBounds_maxX - calcBounds_minX;
    double h = calcBounds_maxY - calcBounds_minY;
    int heatFlowHoriz = (h >= w);
    double d = gridRes;  // mm
    double d_m = d / 1000.0;
    int resX = (int)ceil(w / d);
    int resY = (int)ceil(h / d);
    int N = resX * resY;

    // Ajustar calcBounds al múltiplo exacto
    calcBounds_maxX = calcBounds_minX + resX * d;
    calcBounds_maxY = calcBounds_minY + resY * d;

    // Reserva de memoria dinámica (usaremos malloc para grandes tamaños)
    uint8_t *isSolid = (uint8_t*)calloc(N, sizeof(uint8_t));
    float *kGrid = (float*)calloc(N, sizeof(float));
    uint8_t *lineBlocker = (uint8_t*)calloc(N, sizeof(uint8_t));
    uint8_t *boundGrid = (uint8_t*)calloc(N, sizeof(uint8_t));
    uint8_t *isOuterAir = (uint8_t*)calloc(N, sizeof(uint8_t));
    uint8_t *cavityVisited = (uint8_t*)calloc(N, sizeof(uint8_t));
    uint8_t *airTypeGrid = (uint8_t*)calloc(N, sizeof(uint8_t));
    float *T = (float*)malloc(N * sizeof(float));
    int *queue = (int*)malloc(N * sizeof(int));
    int *solidIdx = (int*)malloc(N * sizeof(int));
    int solidCount = 0;
    int *cavCells = (int*)malloc(N * sizeof(int));

    if (!isSolid || !kGrid || !lineBlocker || !boundGrid || !isOuterAir ||
        !cavityVisited || !airTypeGrid || !T || !queue || !solidIdx || !cavCells) {
        // fallback: liberamos y devolvemos error
        free(isSolid); free(kGrid); free(lineBlocker); free(boundGrid); free(isOuterAir);
        free(cavityVisited); free(airTypeGrid); free(T); free(queue); free(solidIdx); free(cavCells);
        *T_out = NULL; *kGrid_out = NULL; return;
    }

    // --------------- 2. Rasterización por scanline ---------------
    // Recorrer polígonos
    int offset = 0;
    for (int p = 0; p < num_polygons; p++) {
        int npoints = polygons_lengths[p];
        Point* pts = (Point*)(polygons_flat + offset);
        offset += 2 * npoints; // cada punto son 2 doubles
        // Encontrar matIdx: está al final del array de puntos? El flat original de JS es [..., matIdx]
        // Como no tenemos matIdx explícito, asumimos que cada polígono tiene al final un double con matIdx.
        // En el array polygons_flat organizamos: [x0,y0,x1,y1,...,xn,yn, matIdx]
        int matIdx = (int)(polygons_flat[offset - 1]); // el último número
        double matK = (matIdx >= 0 && matIdx < num_materials) ? materials_k[matIdx] : 0.0;

        // Bounding box en píxeles
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (int i = 0; i < npoints; i++) {
            if (pts[i].x < minX) minX = pts[i].x;
            if (pts[i].x > maxX) maxX = pts[i].x;
            if (pts[i].y < minY) minY = pts[i].y;
            if (pts[i].y > maxY) maxY = pts[i].y;
        }
        int jMin = fmax(0, floor((minX - calcBounds_minX) / d));
        int jMax = fmin(resX-1, ceil((maxX - calcBounds_minX) / d));
        int iMin = fmax(0, floor((minY - calcBounds_minY) / d));
        int iMax = fmin(resY-1, ceil((maxY - calcBounds_minY) / d));

        // Precalcular aristas para scanline
        typedef struct { double yMin, yMax, y1, x1, invSlope; } Edge;
        Edge* edges = (Edge*)malloc(npoints * sizeof(Edge));
        int edgeCount = 0;
        for (int i = 0; i < npoints; i++) {
            Point p1 = pts[i];
            Point p2 = pts[(i+1)%npoints];
            if (fabs(p2.y - p1.y) < 1e-9) continue;
            double dy = p2.y - p1.y;
            double dx = p2.x - p1.x;
            edges[edgeCount].yMin = fmin(p1.y, p2.y);
            edges[edgeCount].yMax = fmax(p1.y, p2.y);
            edges[edgeCount].y1 = p1.y;
            edges[edgeCount].x1 = p1.x;
            edges[edgeCount].invSlope = dx / dy;
            edgeCount++;
        }

        for (int i = iMin; i <= iMax; i++) {
            double yCenter = calcBounds_minY + i * d + d/2;
            // intersecciones
            double* xs = (double*)malloc(edgeCount * sizeof(double));
            int xsCount = 0;
            for (int e = 0; e < edgeCount; e++) {
                if (yCenter < edges[e].yMin || yCenter > edges[e].yMax) continue;
                xs[xsCount++] = edges[e].x1 + (yCenter - edges[e].y1) * edges[e].invSlope;
            }
            // ordenar
            for (int a = 0; a < xsCount-1; a++) {
                for (int b = a+1; b < xsCount; b++) {
                    if (xs[b] < xs[a]) { double tmp = xs[a]; xs[a] = xs[b]; xs[b] = tmp; }
                }
            }
            for (int k = 0; k < xsCount-1; k += 2) {
                double xLeft = xs[k];
                double xRight = xs[k+1];
                int jLeft = floor((xLeft - calcBounds_minX) / d);
                int jRight = ceil((xRight - calcBounds_minX) / d);
                int jStart = fmax(jMin, jLeft);
                int jEnd = fmin(jMax, jRight-1);
                int rowBase = i * resX;
                for (int j = jStart; j <= jEnd; j++) {
                    int idx = rowBase + j;
                    if (!isSolid[idx]) {
                        isSolid[idx] = 1;
                        kGrid[idx] = (float)matK;
                    }
                }
            }
            free(xs);
        }
        free(edges);
    }

    // --------------- 3. Contornos y bloqueadores adiabáticos ---------------
    for (int e = 0; e < num_boundary_edges; e++) {
        BoundaryEdge* be = (BoundaryEdge*)(boundary_edges_flat + e*5); // x1,y1,x2,y2,type (cada uno double, type como double)
        double x1 = be->p1.x, y1 = be->p1.y, x2 = be->p2.x, y2 = be->p2.y;
        uint8_t type = (uint8_t)be->type;
        if (type == 3) {
            int horiz = (fabs(x2 - x1) > fabs(y2 - y1));
            if (horiz) {
                double py = (y1 + y2) / 2;
                int iRow = (int)floor((py - calcBounds_minY) / d);
                if (iRow >= 0 && iRow < resY) {
                    memset(lineBlocker + iRow * resX, 1, resX);
                }
            } else {
                double px = (x1 + x2) / 2;
                int jCol = (int)floor((px - calcBounds_minX) / d);
                if (jCol >= 0 && jCol < resX) {
                    for (int r = 0; r < resY; r++) lineBlocker[r * resX + jCol] = 1;
                }
            }
        } else if (type != 0) {
            double edx = x2 - x1, edy = y2 - y1;
            double len = sqrt(edx*edx + edy*edy);
            int steps = (int)ceil(len / d * 3);
            for (int s = 0; s <= steps; s++) {
                double px = x1 + ((double)s / steps) * edx;
                double py = y1 + ((double)s / steps) * edy;
                int cj = (int)floor((px - calcBounds_minX) / d);
                int ci = (int)floor((py - calcBounds_minY) / d);
                for (int di = -1; di <= 1; di++) {
                    int ni = ci + di;
                    if (ni < 0 || ni >= resY) continue;
                    int rB = ni * resX;
                    for (int dj = -1; dj <= 1; dj++) {
                        int nj = cj + dj;
                        if (nj >= 0 && nj < resX) boundGrid[rB + nj] = type;
                    }
                }
            }
        }
    }

    // --------------- 4. Inundación aire exterior ---------------
    int qHead = 0, qTail = 0;
    for (int i = 0; i < resY; i++) {
        int left = i * resX, right = left + resX - 1;
        if (!isSolid[left] && !lineBlocker[left]) { isOuterAir[left] = 1; queue[qTail++] = left; }
        if (!isSolid[right] && !lineBlocker[right]) { isOuterAir[right] = 1; queue[qTail++] = right; }
    }
    for (int j = 0; j < resX; j++) {
        int top = j, bottom = (resY-1)*resX + j;
        if (!isSolid[top] && !lineBlocker[top] && !isOuterAir[top]) { isOuterAir[top] = 1; queue[qTail++] = top; }
        if (!isSolid[bottom] && !lineBlocker[bottom] && !isOuterAir[bottom]) { isOuterAir[bottom] = 1; queue[qTail++] = bottom; }
    }
    int dirs4[4] = {-resX, 1, resX, -1};
    while (qHead < qTail) {
        int idx = queue[qHead++];
        int cj = idx % resX;
        for (int k = 0; k < 4; k++) {
            if (k == 1 && cj == resX-1) continue;
            if (k == 3 && cj == 0) continue;
            int nidx = idx + dirs4[k];
            if (!isSolid[nidx] && !isOuterAir[nidx] && !lineBlocker[nidx]) {
                isOuterAir[nidx] = 1;
                queue[qTail++] = nidx;
            }
        }
    }

    // --------------- 5. Cavidades ---------------
    double hr_base = 4 * SIGMA * pow(Tm, 3) * (1.0 / (2.0/emiss - 1.0));
    for (int i = 0; i < resY; i++) {
        int rB = i * resX;
        for (int j = 0; j < resX; j++) {
            int idx = rB + j;
            if (!isSolid[idx] && !isOuterAir[idx] && !cavityVisited[idx] && !lineBlocker[idx]) {
                qHead = 0; qTail = 0;
                queue[qTail++] = idx;
                cavityVisited[idx] = 1;
                int min_i = i, max_i = i, min_j = j, max_j = j, cavCount = 0;
                while (qHead < qTail) {
                    int cidx = queue[qHead++];
                    int ci = cidx / resX, cj2 = cidx % resX;
                    cavCells[cavCount++] = cidx;
                    if (ci < min_i) min_i = ci; if (ci > max_i) max_i = ci;
                    if (cj2 < min_j) min_j = cj2; if (cj2 > max_j) max_j = cj2;
                    for (int k = 0; k < 4; k++) {
                        if (k == 1 && cj2 == resX-1) continue;
                        if (k == 3 && cj2 == 0) continue;
                        int nidx = cidx + dirs4[k];
                        if (!isSolid[nidx] && !isOuterAir[nidx] && !cavityVisited[nidx] && !lineBlocker[nidx]) {
                            cavityVisited[nidx] = 1;
                            queue[qTail++] = nidx;
                        }
                    }
                }
                double width_cav = (max_j - min_j + 1) * d_m;
                double height_cav = (max_i - min_i + 1) * d_m;
                double d_cav = heatFlowHoriz ? width_cav : height_cav;
                double b_cav = heatFlowHoriz ? height_cav : width_cav;
                double ratio = d_cav / (b_cav > 0.001 ? b_cav : 0.001);
                double phi = 0.5 + 0.5 * sqrt(1 + ratio*ratio) - 0.5 * ratio;
                double lambda_eq = 0.025 + hr_base * phi * d_cav;
                for (int c = 0; c < cavCount; c++) {
                    int cidx = cavCells[c];
                    kGrid[cidx] = (float)lambda_eq;
                    isSolid[cidx] = 1;
                }
            }
        }
    }

    // --------------- 6. Tipos de aire ---------------
    qHead = 0; qTail = 0;
    for (int i = 0; i < N; i++) {
        if (boundGrid[i] && isOuterAir[i]) {
            airTypeGrid[i] = boundGrid[i];
            queue[qTail++] = i;
        }
    }
    while (qHead < qTail) {
        int idx = queue[qHead++];
        int cj = idx % resX;
        uint8_t type = airTypeGrid[idx];
        for (int k = 0; k < 4; k++) {
            if (k == 1 && cj == resX-1) continue;
            if (k == 3 && cj == 0) continue;
            int nidx = idx + dirs4[k];
            if (isOuterAir[nidx] && airTypeGrid[nidx] == 0) {
                airTypeGrid[nidx] = type;
                queue[qTail++] = nidx;
            }
        }
    }

    // --------------- 7. Precálculo de vecinos y conductancias ---------------
    // Primero contar sólidos
    solidCount = 0;
    for (int i = 0; i < N; i++) if (isSolid[i]) solidIdx[solidCount++] = i;

    // Preparar listas de adyacencia precalculadas (similar a la versión JS)
    int* neighIdx = (int*)malloc(solidCount * 4 * sizeof(int));
    float* neighC = (float*)malloc(solidCount * 4 * sizeof(float));
    double* diag = (double*)malloc(solidCount * sizeof(double));
    double* bSrc = (double*)malloc(solidCount * sizeof(double));
    float* invDiag = (float*)malloc(solidCount * sizeof(float));
    float* k_local = (float*)malloc(solidCount * sizeof(float));

    for (int s = 0; s < solidCount; s++) {
        int idx = solidIdx[s];
        float k = kGrid[idx];
        k_local[s] = k;
        int i = idx / resX, j = idx % resX;
        double dSum = 0, bSum = 0;
        int base = s * 4;
        int edge = 0;

        // macro para añadir vecino
        #define ADD_NEIGHBOR(nidx_val) do { \
            if ((nidx_val) >= 0 && (nidx_val) < N) { \
                if (isSolid[(nidx_val)]) { \
                    float k2 = kGrid[(nidx_val)]; \
                    float C = 2*k*k2/(k+k2); \
                    neighIdx[base+edge] = (nidx_val); \
                    neighC[base+edge] = C; \
                    dSum += C; \
                } else if (isOuterAir[(nidx_val)]) { \
                    uint8_t t = airTypeGrid[(nidx_val)]; \
                    if (t==1 || t==2 || t==4) { \
                        double Rs = (t==1)? Rse : Rsi; \
                        double T_air = (t==1)? tExt : tInt; \
                        double C = (2*k*d_m) / (d_m + 2*k*Rs); \
                        neighIdx[base+edge] = -1; /* marca borde */ \
                        neighC[base+edge] = (float)C; \
                        dSum += C; \
                        bSum += C * T_air; \
                    } else { \
                        neighIdx[base+edge] = -1; neighC[base+edge]=0; \
                    } \
                } else { \
                    neighIdx[base+edge] = -1; neighC[base+edge]=0; \
                } \
            } else { \
                neighIdx[base+edge] = -1; neighC[base+edge]=0; \
            } \
            edge++; \
        } while(0)

        if (j > 0) ADD_NEIGHBOR(idx-1); else { neighIdx[base]=-1; neighC[base]=0; edge++; }
        if (j < resX-1) ADD_NEIGHBOR(idx+1); else { neighIdx[base+1]=-1; neighC[base+1]=0; edge++; }
        if (i > 0) ADD_NEIGHBOR(idx-resX); else { neighIdx[base+2]=-1; neighC[base+2]=0; edge++; }
        if (i < resY-1) ADD_NEIGHBOR(idx+resX); else { neighIdx[base+3]=-1; neighC[base+3]=0; edge++; }

        diag[s] = dSum;
        bSrc[s] = bSum;
        invDiag[s] = (float)(1.0 / dSum);
    }

    // --------------- 8. Inicialización de temperaturas ---------------
    double initT = (tExt + tInt) / 2;
    for (int i = 0; i < N; i++) {
        if (isSolid[i]) {
            T[i] = (float)initT;
        } else {
            uint8_t t = airTypeGrid[i];
            if (t == 2 || t == 4) T[i] = (float)tInt;
            else T[i] = (float)tExt;
        }
    }

    // --------------- 9. Solver SOR con parada temprana ---------------
    int maxRes = (resX > resY) ? resX : resY;
    int iterMax = (maxRes <= 100) ? 5000 : (maxRes <= 250 ? 10000 : 20000);
    double omega = 2.0 / (1.0 + sin(M_PI / maxRes));
    double tolerance = 1e-6;
    int checkInterval = 200;
    int actualIterations = 0;
    bool converged = false;

    for (int chunk = 0; chunk < iterMax && !converged; chunk += checkInterval) {
        for (int step = 0; step < checkInterval; step++) {
            actualIterations++;
            if (step == checkInterval - 1) {
                double maxChange = 0;
                for (int s = 0; s < solidCount; s++) {
                    double sum = bSrc[s];
                    int base = s*4;
                    for (int n = 0; n < 4; n++) {
                        int nidx = neighIdx[base+n];
                        if (nidx >= 0) sum += neighC[base+n] * T[nidx];
                    }
                    int idx = solidIdx[s];
                    double oldT = T[idx];
                    double newT = oldT + omega * (sum * invDiag[s] - oldT);
                    T[idx] = (float)newT;
                    double change = fabs(newT - oldT);
                    if (change > maxChange) maxChange = change;
                }
                if (maxChange < tolerance) { converged = true; break; }
            } else {
                for (int s = 0; s < solidCount; s++) {
                    double sum = bSrc[s];
                    int base = s*4;
                    for (int n = 0; n < 4; n++) {
                        int nidx = neighIdx[base+n];
                        if (nidx >= 0) sum += neighC[base+n] * T[nidx];
                    }
                    int idx = solidIdx[s];
                    T[idx] = (float)(T[idx] + omega * (sum * invDiag[s] - T[idx]));
                }
            }
            if (converged) break;
        }
    }

    // --------------- 10. Cálculo de flujo y temperatura mínima ---------------
    double flux = 0, minTsi = 1e9;
    int minIdx = -1;
    for (int s = 0; s < solidCount; s++) {
        int idx = solidIdx[s];
        double Tcell = T[idx];
        float k = k_local[s];
        int base = s*4;
        for (int d = 0; d < 4; d++) {
            int nidx = neighIdx[base+d];
            if (nidx == -1 && neighC[base+d] > 0) {
                int realNidx;
                if (d == 0) realNidx = idx-1;
                else if (d == 1) realNidx = idx+1;
                else if (d == 2) realNidx = idx-resX;
                else realNidx = idx+resX;
                uint8_t type = airTypeGrid[realNidx];
                if (type == 2 || type == 4) {
                    double C = neighC[base+d];
                    double qVal = C * (tInt - Tcell);
                    double tSurf = tInt - (qVal / d_m) * Rsi;
                    if (tSurf < minTsi) { minTsi = tSurf; minIdx = idx; }
                    if (type == 2) flux += qVal;
                }
            }
        }
    }

    if (minTsi == 1e9) minTsi = tInt;
    int minI = (minIdx != -1) ? (minIdx / resX) : -1;
    int minJ = (minIdx != -1) ? (minIdx % resX) : -1;
    *lastTsiMinX = (minIdx != -1) ? (calcBounds_minX + minJ * d + d/2) : 0;
    *lastTsiMinY = (minIdx != -1) ? (calcBounds_minY + minI * d + d/2) : 0;
    double deltaT = fabs(tInt - tExt);
    if (deltaT < 1e-9) deltaT = 1.0;
    double L2D = fabs(flux) / deltaT;
    double Uf = L2D / (Lp / 1000.0); // Lp viene en mm desde el frontend, convertimos a metros
    double FRsi = (minTsi - tExt) / deltaT;
    if (FRsi > 1) FRsi = 1;
    if (FRsi < 0) FRsi = 0;
    *lastL2D = L2D;
    *lastU = Uf;
    *lastFlux = flux;
    *lastFRsi = FRsi;
    *lastTsiMin = minTsi;

    // --------------- 11. Devolver resultados ---------------
    *resX_out = resX;
    *resY_out = resY;
    *T_out = (double*)malloc(N * sizeof(double));
    *kGrid_out = (double*)malloc(N * sizeof(double));
    for (int i = 0; i < N; i++) {
        (*T_out)[i] = T[i];
        (*kGrid_out)[i] = isSolid[i] ? kGrid[i] : 0.0;
    }

    // Liberar memoria local (excepto las salidas)
    free(isSolid); free(kGrid); free(lineBlocker); free(boundGrid); free(isOuterAir);
    free(cavityVisited); free(airTypeGrid); free(T); free(queue); free(solidIdx); free(cavCells);
    free(neighIdx); free(neighC); free(diag); free(bSrc); free(invDiag); free(k_local);
}
