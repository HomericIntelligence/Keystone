set shell := ["bash", "-c"]

default:
  @just --list

build:
  make compile NATIVE=1

test:
  make test NATIVE=1

lint:
  make lint NATIVE=1

format:
  make format NATIVE=1

format-check:
  make format.check NATIVE=1

clean:
  make clean

start:
  make docker.up

status:
  docker-compose ps

benchmark:
  make benchmark NATIVE=1

ci:
  make ci NATIVE=1
