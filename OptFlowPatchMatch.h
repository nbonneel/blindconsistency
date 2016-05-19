#pragma once

#include "patchmatch\nn.h"

template<typename T, typename Tflow>
void opt_flow_patchmatch(const T* imgA, const T* imgB, int W, int H, Tflow* optflow) { // images in the range 0..1

	Params p;
	RecomposeParams rp;
	init_params(&p);
	p.cores = omp_get_max_threads();

	PATCHBITMAP* a = create_bitmap(W, H);
	PATCHBITMAP* b = create_bitmap(W, H);

	for(int i=0; i<H; i++) {
		int* lineA = (int*)a->line[i];
		int* lineB = (int*)b->line[i];
		for (int j=0; j<W; j++) {
			T ar = imgA[(i*W+j)*3+0], ag=imgA[(i*W+j)*3+1], ab=imgA[(i*W+j)*3+2];
			T br = imgB[(i*W+j)*3+0], bg=imgB[(i*W+j)*3+1], bb=imgB[(i*W+j)*3+2];
			unsigned char uar = (unsigned char)std::min((T)255, std::max((T)0, (T)(ar*255.))) , uag = (unsigned char)std::min((T)255, std::max((T)0, (T)(ag*255.))), uab = (unsigned char)std::min((T)255, std::max((T)0, (T)(ab)));
			unsigned char ubr = (unsigned char)std::min((T)255, std::max((T)0, (T)(br*255.))) , ubg = (unsigned char)std::min((T)255, std::max((T)0, (T)(bg*255.))), ubb = (unsigned char)std::min((T)255, std::max((T)0, (T)(bb)));

			lineA[j] = uar+(uag<<8)+(uab<<16);
			lineB[j] = ubr+(ubg<<8)+(ubb<<16);
		}
	}


	PATCHBITMAP *ann = NULL; // NN field
	PATCHBITMAP *annd_final = NULL; // NN patch distance field
	PATCHBITMAP *ann_sim_final = NULL;

	ann = init_nn(&p, a, b, NULL, NULL, NULL, 1, NULL, NULL);

	
	PATCHBITMAP *annd = init_dist(&p, a, b, ann, NULL, NULL, NULL);
	nn(&p, a, b, ann, annd, NULL, NULL, 0, 0, &rp, 0, 0, 0, NULL, p.cores, NULL, NULL); 
	annd_final = annd;

	for (int y = 0; y < H; y++) {
		int *ann_row = (int *) ann->line[y];
		int *annd_row = (int *) annd_final->line[y];
		for (int x = 0; x < W; x++) {
			int pp = ann_row[x];			
			optflow[(y*W+x)*2+0] = (Tflow)INT_TO_X(pp);
			optflow[(y*W+x)*2+1] = (Tflow)INT_TO_Y(pp);
		}
	}

	destroy_bitmap(a);
	destroy_bitmap(b);
	destroy_bitmap(ann);
	destroy_bitmap(ann_sim_final);
	destroy_bitmap(annd);

}
