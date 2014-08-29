// read distribution of insert length, etc
#define SEQAN_HAS_ZLIB 1
#include "utils.h"
#ifndef DELETE_UTILS_H
#define DELETE_UTILS_H

enum T_READ {useless_read, unknow_read, mid_read, clip_read}; // used for deletions

class GENO_PROB {
 public:
  float g0, g1, g2;
  string phredStr;
  int geno;
 GENO_PROB(float g0, float g1, float g2) : g0(g0), g1(g1), g2(g2), phredStr(""), geno(0) {}
 GENO_PROB(float g0, float g1, float g2, string ps, int _g) : g0(g0), g1(g1), g2(g2), phredStr(ps), geno(_g) {}
};

bool get_align_pos(int aluBegin, int aluEnd, int beginPos, int endPos, int &ref_a, int &ref_b, int &read_a, int &read_b, seqan::BamAlignmentRecord &record);
bool split_global_align(seqan::CharString &fa_seq, seqan::BamAlignmentRecord &record, int read_a, int read_b, int &score, int &align_len);
T_READ classify_read(seqan::BamAlignmentRecord & record, int aluBegin, int aluEnd, FastaFileHandler *fasta_fh, bool debug = false);

bool combine_pns_vcf(string path0, string f_in_suffix, string f_out, vector <string> &pns, vector <string> & chrns, map <string, std::set<int> > & chrn_aluBegin, float llh_th, string ref_name);

string tread_toStr(T_READ td);

#endif 
