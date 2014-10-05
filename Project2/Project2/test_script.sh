
TARGET_DIR=test/*

for x in ${TARGET_DIR} 
do
	for i in 1 2 3 4 5 6 7 8
	do
		./$x 100 10000 >> prodcons_results.txt
	done
done
