all: build

build:
	clang -g -o bake $(wildcard *.c) -std=c23 -I. -Itomlc99/ -Ltomlc99/ -ltoml
