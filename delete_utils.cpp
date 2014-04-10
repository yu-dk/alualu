#include "delete_utils.h"

bool get_align_pos(int aluBegin, int aluEnd, int beginPos, int endPos, int &ref_a, int &ref_b, int &read_a, int &read_b, seqan::BamAlignmentRecord &record){
  unsigned nl = length(record.cigar) - 1; // ignore complicated alignment between S and M
  int len_read = length(record.seq);
  int len_clip_toAlign = 0;
  if ( abs(beginPos - aluEnd) <= BOUNDARY_OFFSET and record.cigar[0].operation == 'S') {  //eg. S41M60, has_soft_first 
    len_clip_toAlign = record.cigar[0].count;
    ref_a = aluBegin - len_clip_toAlign;
    ref_b = aluBegin;
    read_a = 0;
    read_b = len_clip_toAlign;
  } else if (abs(endPos - aluBegin) <= BOUNDARY_OFFSET and record.cigar[nl].operation == 'S' ) { // eg. M27S93, has_soft_last
    len_clip_toAlign = record.cigar[nl].count;
    ref_a = aluEnd;
    ref_b = aluEnd + len_clip_toAlign;
    read_a = len_read - len_clip_toAlign;
    read_b = len_read;
  }
  if ( !len_clip_toAlign ) return false;
  /* beginning of reads is bad quality. we cut a few bp before aligning
     eg: 10bp cut 1bp, 110bp(90bp) cut 10bp;*/
  int cut_bp = len_read > 110 ? round( 0.11 * len_clip_toAlign - 0.13 ) : round( 0.1 * len_clip_toAlign ); 
  if ( hasFlagRC(record) and read_a == 0) read_a += cut_bp;    
  if ( (!hasFlagRC(record)) and read_b == len_read) read_b -= cut_bp;
  return read_a < read_b;
}

string enum_to_string(T_READ x){
  if ( x == useless_read ) return "useless_read";
  if ( x == unknow_read ) return "unknow_read";
  if ( x == mid_read ) return "mid_read";
  if ( x == clip_read ) return "clip_read";
  return "ERROR!" ;
};

bool split_global_align(seqan::CharString &fa_seq, seqan::BamAlignmentRecord &record, int read_a, int read_b){
  // no need to use seed or chain, since don't know exact position to start mapping
  seqan::Score<int> scoringScheme(1, -3, -2, -5); // match, mismatch, gap extend, gap open
  TAlign align;
  int align_len, score;
  int min_align_len = max(CLIP_BP, (int) (0.85 * (read_b - read_a - BOUNDARY_OFFSET) ) ); 
  resize(rows(align), 2);
  assignSource(row(align,0), infix(record.seq, read_a, read_b) );  // 2,3
  assignSource(row(align,1), fa_seq);   // 1,4

  // free gap both ends
  score = globalAlignment(align, scoringScheme, seqan::AlignConfig<true, true, true, true>()); 
  align_len = min(toViewPosition(row(align, 0), read_b - read_a), toViewPosition(row(align, 1), length(fa_seq)))
    - max(toViewPosition(row(align, 0), 0), toViewPosition(row(align, 1), 0));
  //cout << clippedBeginPosition(row(align, 0)) << " " << clippedBeginPosition(row(align, 1)) << " " <<  endl;
  //cout << "score1 " << score << " " << align_len << " " << min_align_score(align_len) << " " << min_align_len << " " << endl;
  //cout << align << endl;
  if ( align_len >= min_align_len and score >= min_align_score(align_len) ) return true; 
  
  // AlignConfig 2=true: free end gaps for record.seq
  score = globalAlignment(align, scoringScheme, seqan::AlignConfig<false, true, true, false>()); 
  align_len = min(toViewPosition(row(align, 0), read_b - read_a), toViewPosition(row(align, 1), length(fa_seq)))
    - max(toViewPosition(row(align, 0), 0), toViewPosition(row(align, 1), 0));
  return ( align_len >= min_align_len and score >= min_align_score(align_len) );
}


T_READ classify_read(seqan::BamAlignmentRecord & record, int align_len, int aluBegin, int aluEnd, seqan::FaiIndex &faiIndex, unsigned fa_idx, bool read_is_left){  
  int beginPos = record.beginPos;
  int endPos = record.beginPos + align_len;  
  // more unknow_read, more information used 
  if ( (!read_is_left) and beginPos > aluEnd - BOUNDARY_OFFSET and  
       (!has_soft_first(record, CLIP_BP)) and record.pNext + DEFAULT_READ_LEN < aluBegin + BOUNDARY_OFFSET )
    return unknow_read;
  // left reads, and we won't check its right pair afterwards
  if ( read_is_left and endPos < aluBegin - BOUNDARY_OFFSET and  
       (!has_soft_last(record, CLIP_BP)) and record.pNext >=  aluEnd + ALU_FLANK )
    return unknow_read;
    
  if ( (has_soft_first(record, CLIP_BP) and abs(beginPos - aluEnd) <= BOUNDARY_OFFSET ) or 
       ( has_soft_last(record, CLIP_BP) and abs(endPos - aluBegin) <= BOUNDARY_OFFSET ) ) {    
    int ref_a, ref_b, read_a, read_b;
    if ( get_align_pos(aluBegin, aluEnd, beginPos, endPos, ref_a, ref_b, read_a, read_b, record)) {
      seqan::CharString fa_seq = fasta_seq(faiIndex, fa_idx, ref_a - BOUNDARY_OFFSET, ref_b + BOUNDARY_OFFSET, true);
      if (split_global_align(fa_seq, record, read_a, read_b)) return clip_read;
    }
    return useless_read; // if left reads, we don't know the type yet
  }
      
  // only consider as mid_read if very certain, otherwise look at its pair later
  if ( endPos > aluBegin + BOUNDARY_OFFSET and beginPos < aluEnd - BOUNDARY_OFFSET) 
    if (!not_all_match(record))  return mid_read;
  
  return useless_read;  // alu_flank is too larger, we have a lot reads not useful 
}

bool check_delete_region(string const & bam_input, string const &bai_input, string const & fa_input,  string chrn, int beginPos, int endPos ){
  seqan::FaiIndex faiIndex;
  unsigned fa_idx = 0;
  assert (!read(faiIndex, fa_input.c_str()) );      
  assert (getIdByName(faiIndex, chrn, fa_idx));
  TNameStore      nameStore;
  TNameStoreCache nameStoreCache(nameStore);
  TBamIOContext   context(nameStore, nameStoreCache);
  seqan::BamHeader header;
  seqan::Stream<seqan::Bgzf> inStream;  
  open(inStream, bam_input.c_str(), "r");
  seqan::BamIndex<seqan::Bai> baiIndex;
  assert( !read(baiIndex, bai_input.c_str())) ;
  assert ( !readRecord(header, context, inStream, seqan::Bam()) );
  int rID = 0;
  assert ( getIdByName(nameStore, chrn, rID, nameStoreCache) ); // change context ??
  seqan::BamAlignmentRecord record;  
  bool hasAlignments = false;
  if (!jumpToRegion(inStream, hasAlignments, context, rID, beginPos, endPos, baiIndex)) return false;
  if (!hasAlignments) return false;

  map <seqan::CharString, T_READ> special_read;  
  map <seqan::CharString, string> unknow_info;
  map <seqan::CharString, string> special_info;

  //int aluBegin = beginPos + 600;
  //int aluEnd = endPos - 600;

  while (!atEnd(inStream)) {
    assert (!readRecord(record, context, inStream, seqan::Bam())); 
    if (record.rID != rID || record.beginPos >= endPos) break;
    if (record.beginPos < beginPos) continue;            
    if ( !QC_delete_read(record) ) continue;
    //int align_len = getAlignmentLengthInRef(record);    

    map <seqan::CharString, string>::iterator ii;
    cout << special_info.size() << " " << unknow_info.size() << endl;
    for ( ii = special_info.begin(); ii != special_info.end(); ii++)
      cout << ii->first << " " << ii->second << endl;
    for ( ii = unknow_info.begin(); ii != unknow_info.end(); ii++)
      cout << ii->first << " " << ii->second << endl;
  } 
  return true;
}

void genotype_prob(map < string, vector<int> >  &insertlen_rg, map <string, EmpiricalPdf *> &empiricalpdf_rg, int alu_len, float *log_p){
  for (int i = 0; i < 3; i++) log_p[i] = 0;
  EmpiricalPdf *empiricalpdf;
  for (map < string, vector<int> >::iterator irg=insertlen_rg.begin(); irg!=insertlen_rg.end(); irg++) {
    empiricalpdf = empiricalpdf_rg[irg->first];
    for (vector<int>::iterator ir = irg->second.begin(); ir != irg->second.end(); ir++ ) {
      float p_y = empiricalpdf->pdf_obs(*ir);
      float p_z = empiricalpdf->pdf_obs(alu_len + *ir);      
      log_p[0] += log(p_y);
      log_p[1] += log(0.67 * p_y + 0.33 * p_z);
      log_p[2] += log(p_z); 
    }
  }
  normalize_prob(log_p);
}

bool normalize_prob(float *log_p){
  float r20 = exp(log_p[2] - log_p[0]); 
  float r10 = exp(log_p[1] - log_p[0]); 
  if ( max(r20, r10) < 1e-5 ) return false;
  float r_sum = r20 + r10 + 1;
  log_p[0] = 1./r_sum;
  log_p[1] = r10/r_sum;
  log_p[2] = r20/r_sum;
  return true;
}

EmpiricalPdf::EmpiricalPdf(string pdf_file){ 
  int pos;
  float posp;
  bin_width = 0;
  ifstream fin( pdf_file.c_str());
  assert (fin);
  fin >> pos >> posp;
  prob_vec[pos] = posp;
  min_len = pos;
  min_prob = posp;
  while (fin >> pos >> posp) {
    prob_vec[pos] = posp;
    min_prob = min_prob > posp ? posp : min_prob;      
    if (!bin_width) bin_width = pos - min_len;
  }
  max_len = pos;
  fin.close();
  cerr << "read pdf dist " << pdf_file << endl;
  cerr << min_len << " " << max_len << " " << bin_width << " " << min_prob << endl;
}

float EmpiricalPdf::pdf_obs(int insertlen) {
  if (insertlen >= max_len || insertlen <= min_len) return min_prob;
  int nearby_pos = min_len + (insertlen-min_len)/bin_width*bin_width;
  map<int, float >::iterator it = prob_vec.find(nearby_pos);
  if ( it != prob_vec.end())  return it->second;
  return min_prob;
}   
