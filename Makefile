CC=gcc
CFLAGS=-Wall

tesh: tesh.c
	gcc tesh.c -o tesh -Wall -ldl
