# fast_net

## Getting Started

Clone the repo with submodules:

```bash
git clone --recurse-submodules git@github.com:sssemil/fast_net.git 
```

Build:

```bash 
mkdir build
cd build
cmake ..
make
```

## Docker

```
docker build -t emilss/fast_net .     
docker push emilss/fast_net:latest
docker run --privileged --cap-add SYS_ADMIN -it --rm emilss/fast_net:latest
```