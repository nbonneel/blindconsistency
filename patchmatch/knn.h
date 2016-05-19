
/* PatchMatch finding k-NN, with similarity transform of patches (rotation+scale). */

#ifndef _knn_h
#define _knn_h

#include "nn.h"
#include "simnn.h"
#include "vecnn.h"

#define SAVE_DIST           0

#define VBMP VECBITMAP<int>

PATCHBITMAP *greyscale(PATCHBITMAP *a);
PATCHBITMAP *greyscale16(PATCHBITMAP *a);
PATCHBITMAP *greyscale_to_color(PATCHBITMAP *a);
PATCHBITMAP *gaussian_blur16(PATCHBITMAP *a, double sigma);
PATCHBITMAP *greyscale16_to_color(PATCHBITMAP *a);
PATCHBITMAP *gaussian_deriv_angle(PATCHBITMAP *a, double sigma, PATCHBITMAP **dx_out=NULL, PATCHBITMAP **dy_out=NULL);

PATCHBITMAP *color_gaussian_blur(PATCHBITMAP *a, double sigma, int aconstraint_alpha);

PATCHBITMAP *extract_vbmp(VBMP *bmp, int i);
void insert_vbmp(VBMP *bmp, int i, PATCHBITMAP *a);
//void sort_knn(Params *p, PATCHBITMAP *a, VBMP *ann, VBMP *ann_sim, VBMP *annd);
VBMP *copy_vbmp(VBMP *a);

#define N_PRINCIPAL_ANGLE_SHIFT 8
#define N_PRINCIPAL_ANGLE (1<<N_PRINCIPAL_ANGLE_SHIFT)

class PRINCIPAL_ANGLE { public:
  PATCHBITMAP *angle[N_PRINCIPAL_ANGLE];
};

PRINCIPAL_ANGLE *create_principal_angle(Params *p, PATCHBITMAP *bmp);
void destroy_principal_angle(PRINCIPAL_ANGLE *b);
int get_principal_angle(Params *p, PRINCIPAL_ANGLE *b, int x0, int y0, int scale);

VBMP *knn_init_nn(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *&ann_sim, PRINCIPAL_ANGLE *pa=NULL);
VBMP *knn_init_dist(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim);

void knn(Params *p, PATCHBITMAP *a, PATCHBITMAP *b,
         VBMP *&ann, VBMP *&ann_sim, VBMP *&annd,
         RegionMasks *amask=NULL, PATCHBITMAP *bmask=NULL,
         int level=0, int em_iter=0, RecomposeParams *rp=NULL, int offset_iter=0, int update_type=0, int cache_b=0,
         RegionMasks *region_masks=NULL, int tiles=-1, PRINCIPAL_ANGLE *pa=NULL, int save_first=0);

class KNNWeightFunc { public:
  virtual double weight(double d, int is_center) = 0;
};

class KNNSolverWeightFunc: public KNNWeightFunc { public:
  double param[3];
  KNNSolverWeightFunc(double x[3]);
  virtual double weight(double d, int is_center);
};

class ObjectiveFunc { public:
  virtual double f(double x[]) = 0;
};

double patsearch(ObjectiveFunc *f, double *x, double *ap, int n, int iters);

PATCHBITMAP *knn_vote(Params *p, PATCHBITMAP *b,
                 VBMP *ann, VBMP *ann_sim, VBMP *annd, VBMP *bnn=NULL, VBMP *bnn_sim=NULL,
                 PATCHBITMAP *bmask=NULL, PATCHBITMAP *bweight=NULL,
                 double coherence_weight=COHERENCE_WEIGHT, double complete_weight=COMPLETE_WEIGHT,
                 RegionMasks *amask=NULL, PATCHBITMAP *aweight=NULL, PATCHBITMAP *ainit=NULL, RegionMasks *region_masks=NULL, PATCHBITMAP *aconstraint=NULL, int mask_self_only=0, KNNWeightFunc *weight_func=NULL, double **accum_out=NULL);

PATCHBITMAP *knn_vote_solve(Params *p, PATCHBITMAP *b,
                 VBMP *ann, VBMP *ann_sim, VBMP *annd, int n, PATCHBITMAP *aorig, double weight_out[3]);

void knn_vis(Params *p, PATCHBITMAP *a, VBMP *ann, VBMP *ann_sim, VBMP *annd, int is_bitmap=0, PATCHBITMAP *vote=NULL, PATCHBITMAP *orig=NULL, PATCHBITMAP *vote_uniform=NULL);

void knn_dual_vis(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, int is_bitmap=0, PATCHBITMAP *vote=NULL, PATCHBITMAP *orig=NULL);

double knn_avg_dist(Params *p, VBMP *annd);

void knn_enrich(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, PRINCIPAL_ANGLE *pa=NULL);

void knn_enrich3(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, PRINCIPAL_ANGLE *pa=NULL);

void knn_enrich4(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, PRINCIPAL_ANGLE *pa=NULL);

void knn_inverse_enrich(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, PRINCIPAL_ANGLE *pa=NULL);

void knn_inverse_enrich2(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, PRINCIPAL_ANGLE *pa=NULL);

void knn_check(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann, VBMP *ann_sim, VBMP *annd, int check_duplicates=1);

void save_dist(Params *p, VBMP *annd, const char *suffix);

/* Also changes p->knn to kp. */
void change_knn(Params *p, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *&ann, VBMP *&ann_sim, VBMP *&annd, int kp, PRINCIPAL_ANGLE *pa=NULL);

void combine_knn(Params *p1, Params *p2, PATCHBITMAP *a, PATCHBITMAP *b, VBMP *ann1, VBMP *ann_sim1, VBMP *annd1, VBMP *ann2, VBMP *ann_sim2, VBMP *annd2, VBMP *&ann, VBMP *&ann_sim, VBMP *&annd);

void check_change_knn(Params *p, PATCHBITMAP *a, PATCHBITMAP *b);

void sort_knn(Params *p, VBMP *ann, VBMP *ann_sim, VBMP *annd);

#endif
