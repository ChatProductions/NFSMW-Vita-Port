#ifndef LOADING_DATA_H
#define LOADING_DATA_H
#include <math.h>
typedef struct {unsigned texture,color;float xy[8],uv[8];} LoadingQuad;
static inline int loading_quad_valid(const LoadingQuad*q,const unsigned*ids,unsigned count){
 unsigned i=0;while(i<count&&ids[i]!=q->texture)i++;if(i==count)return 0;
 /* The original BaseStripe repeats 37.5 times; these are legal wrapped UVs. */
 for(unsigned k=0;k<8;k++)if(!isfinite(q->xy[k])||fabsf(q->xy[k])>4096||!isfinite(q->uv[k])||fabsf(q->uv[k])>64)return 0;
 return 1;
}
#endif
