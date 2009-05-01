
CC = gcc
CFLAGS = -ansi -Wall -Wextra -g3

all: test_src yaml2yeast_test

import:
	cp ../Bnf2Yip/yaml.yip yaml.yip

test-diff: yaml2yeast_test
	rm -rf tests/*.error
	./yaml2yeast_test tests 2>&1 | grep -v passed | grep -v 'not implemented' | sed 's/.input: failed.*//;s/.*/clear ; diff -c &.output &.error | less/' > diff.sh
	cat diff.sh

test-src: test_src test_src.sh
	test_src.sh

table.i: table.m4 yaml.yip
	m4 $(^) > $(@)

classify.i: classify.m4 yaml.yip
	m4 $(^) > $(@)

org_functions.i: functions.m4 yaml.yip
	m4 $(^) > $(@)

ct_functions.i: org_functions.i ctrace.rb
	./ctrace.rb notail < org_functions.i > ct_functions.i

by_name.i: by_name.m4 yaml.yip
	m4 $(^) > $(@)

ct_yip.c: yip.c ctrace.rb
	./ctrace.rb < yip.c > ct_yip.c

ct_yip.o: ct_yip.c yip.h table.i classify.i ct_functions.i by_name.i
	cp ct_functions.i functions.i
	$(CC) -g3 -c $(<)

yip.o: yip.c yip.h table.i classify.i org_functions.i by_name.i
	cp org_functions.i functions.i
	$(CC) $(CFLAGS) -c $(<)

ct_yaml2yeast_test.c: yaml2yeast_test.c ctrace.rb
	./ctrace.rb < yaml2yeast_test.c > ct_yaml2yeast_test.c

ct_yaml2yeast_test.o: ct_yaml2yeast_test.c yip.h
	$(CC) -g3 -c $(<)

yaml2yeast_test.o: yaml2yeast_test.c yip.h
	$(CC) $(CFLAGS) -c $(<)

ct_yaml2yeast_test: ct_yaml2yeast_test.o ct_yip.o
	$(CC) $(CFLAGS) -o $(@) $(^)

yaml2yeast_test: yaml2yeast_test.o yip.o
	$(CC) $(CFLAGS) -o $(@) $(^)

test_src: test_src.o yip.o
	$(CC) $(CFLAGS) -o $(@) $(^)

test_src.o: test_src.c yip.h
	$(CC) $(CFLAGS) -c $(<)

clean:
	rm -rf *.o *.i test_src test_src.input test_src.output yaml2yeast_test
