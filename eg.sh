opt="opt3"
${opt3}/alu_delete config.dk preprocess
${opt3}/alu_delete config.dk write_tmps_pn 0
${opt3}/alu_delete config.dk write_vcf_pns

${opt3}/alu_insert config.dk write_tmps_pn 0
${opt3}/alu_insert config.dk combine_pos_pns     ## need to provide file_pn_used
${opt3}/alu_insert config.dk clipReads_pn 0      ## start from here, remove bad reads 
${opt3}/alu_insert config.dk clipReads_pns chr1 
${opt3}/alu_insert config.dk write_tmp0_pn 0   ## writh *tmp0 files
${opt3}/alu_insert config.dk clipPos_pns chr1  ## find exact clipPos ( write chr*_clip_pass file )
${opt3}/alu_insert config.dk write_tmp2_pn 0  ## first count clip and alu reads (*tmp1 files), then genotype calling (*tmp2 files)
${opt3}/alu_insert config.dk fixed_vcf_pns

