
SOURCE := ../../libs

copy:
	make -C $(SOURCE)/libc all
	make -C $(SOURCE)/shell all
	make -C $(SOURCE)/test all
	cp $(SOURCE)/shell/bin/shell shell
	cp $(SOURCE)/test/bin/test test

clean:
	rm shell
	rm test