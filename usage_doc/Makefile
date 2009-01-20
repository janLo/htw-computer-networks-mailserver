usage_doc.pdf: usage_doc.dvi schema.eps
	dvipdf usage_doc.dvi

usage_doc.dvi: usage_doc.tex
	for i in `seq 1 2`; do \
		echo "PASS $i"; \
		latex usage_doc.tex; \
	done

