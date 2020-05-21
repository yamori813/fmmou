#
# 
#

fmmou: fmmou.c
	cc -framework IOKit -framework CoreFoundation fmmou.c -o fmmou

clean:
	rm fmmou
