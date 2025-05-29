# Build Documentation

This document outlines the step-by-step process for setting up and building the project using `vcpkg` and a custom CMake configuration script on a Unix-like environment (e.g., WSL or Ubuntu on Windows).

---

## 1. Clone and Bootstrap vcpkg

Clone the vcpkg repository and bootstrap it:

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
ls -l ./vcpkg
```

---

## 2. Update vcpkg and Install Dependencies

Update the vcpkg package database and install required libraries:

```bash
./vcpkg update
./vcpkg install abseil gtest benchmark
```

---

## 3. Navigate to the Project Directory

Change into your project directory:

```bash
cd ..
```

---

## 4. Prepare the CMake Build Script

Ensure the build script has the correct line endings and is executable:

```bash
dos2unix .github/cmake.sh
chmod +x .github/cmake.sh
```

---

## 5. Run the CMake Build Script

Execute the build script with the necessary flags:

```bash
.github/cmake.sh -D BUILD_SHARED_LIBS=ON \
  -D CMAKE_TOOLCHAIN_FILE=/mnt/c/Users/arita/Documents/DefensePoint/Mend/cpp/re2/vcpkg/scripts/buildsystems/vcpkg.cmake
```
