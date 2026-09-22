PY ?= python3

.PHONY: check report

check:
	$(PY) scripts/docs_check.py --backlog BACKLOG.md
	$(PY) scripts/coverage_map.py --check

report:
	$(PY) scripts/bdd_report.py --repos ..
	$(PY) scripts/code_anchors.py --repos ..
