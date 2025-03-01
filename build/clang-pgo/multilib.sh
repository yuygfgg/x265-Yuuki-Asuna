#!/bin/sh

# ======= PGO Configuration =======
ENABLE_10BIT_PGO=true

CLANG_PROFILE_GEN="-fprofile-instr-generate"
CLANG_PROFILE_USE="-fprofile-instr-use="

# ======= Script Start =======
mkdir -p 8bit 10bit 12bit
if $ENABLE_10BIT_PGO; then
    mkdir -p pgo_profile
fi

TOP_DIR="$(pwd)"

# ======= 12bit Build =======
echo "Building 12bit library..."
cd 12bit
cmake -G "Unix Makefiles" ../../../source -DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=OFF -DENABLE_SHARED=OFF -DENABLE_CLI=OFF -DMAIN12=ON
make ${MAKEFLAGS}
cp libx265.a ../8bit/libx265_main12.a

# ======= 10bit Build =======
cd ../10bit
echo "Building 10bit library..."

if $ENABLE_10BIT_PGO; then
    echo "=== Step 1: Building temporary version with PGO instrumentation ==="
    
    cmake -G "Unix Makefiles" ../../../source -DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=ON \
          -DENABLE_SHARED=OFF -DENABLE_CLI=ON \
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
          -DCMAKE_C_FLAGS="${CLANG_PROFILE_GEN}" \
          -DCMAKE_CXX_FLAGS="${CLANG_PROFILE_GEN}" \
          -DCMAKE_EXE_LINKER_FLAGS="${CLANG_PROFILE_GEN}" \
          -DCMAKE_SHARED_LINKER_FLAGS="${CLANG_PROFILE_GEN}"
    
    make ${MAKEFLAGS}
    
    echo "=== Step 2: Collecting 10bit encoding performance data ==="
    
    # Clean up old profraw files to avoid merging with previous runs
    rm -f "${TOP_DIR}/pgo_profile/x265-*.profraw"
    
    echo "For comprehensive PGO optimization, it's recommended to run multiple encoding tests with different parameters."
    echo "This ensures more code paths are covered, reducing '-Wprofile-instr-unprofiled' warnings."
    
    CONTINUE_TESTING=true
    TEST_COUNT=0
    
    while $CONTINUE_TESTING; do
        TEST_COUNT=$((TEST_COUNT + 1))
        echo "====== Test Sample #${TEST_COUNT} ======"
        
        # Create a unique filename for each test using timestamp and test number
        TIMESTAMP=$(date +%Y%m%d%H%M%S)
        export LLVM_PROFILE_FILE="${TOP_DIR}/pgo_profile/x265-${TIMESTAMP}-test${TEST_COUNT}.profraw"
        
        echo "Profile data will be saved to: $(basename ${LLVM_PROFILE_FILE})"
        
        echo "Please enter an encoding command for performance analysis:"
        echo "Tip: Try different presets, video content types, and encoding parameters"
        echo "Example: ./x265 --preset veryslow --crf 24 input.yuv -o /dev/null"
        echo "Or: vspipe film_script.vpy - --y4m | ./x265 - --y4m --preset medium -o /dev/null"
        echo "Or: vspipe animation_script.vpy - --y4m | ./x265 - --y4m --preset fast --tune animation -o /dev/null"
        read -p "> " PGO_ENCODE_CMD
        
        echo "Executing: $PGO_ENCODE_CMD"
        eval $PGO_ENCODE_CMD
        
        # Verify profile data was created
        if [ -f "$LLVM_PROFILE_FILE" ]; then
            echo "✓ Profile data collected successfully"
            echo "  File size: $(du -h "$LLVM_PROFILE_FILE" | cut -f1)"
        else
            echo "⚠ Warning: No profile data generated for this test"
        fi
        
        read -p "Continue adding more test samples? (y/n): " MORE_TESTS
        if [ "$MORE_TESTS" != "y" ] && [ "$MORE_TESTS" != "Y" ]; then
            CONTINUE_TESTING=false
        fi
    done
    
    echo "=== Step 3: Processing performance data ==="
    cd "${TOP_DIR}/pgo_profile"
    
    # Check if profile data files exist
    PROFRAW_COUNT=$(ls -1 x265-*.profraw 2>/dev/null | wc -l)
    if [ "$PROFRAW_COUNT" -eq 0 ]; then
        echo "Error: No profile data files found. PGO stage may have failed."
        echo "Please ensure the encoding commands executed successfully and generated profile data."
        exit 1
    fi
    
    echo "Found ${PROFRAW_COUNT} profile data files, merging..."
    echo "Profile data files:"
    ls -lh x265-*.profraw
    
    llvm-profdata merge -output=x265.profdata x265-*.profraw
    
    # Store the absolute path of the final profdata file
    PROFILE_DATA_PATH="${TOP_DIR}/pgo_profile/x265.profdata"
    
    echo "=== Step 4: Checking PGO coverage ==="
    echo "Generating file coverage report..."
    X265_EXECUTABLE="${TOP_DIR}/10bit/x265"
    
    # Check if the executable exists
    if [ ! -f "$X265_EXECUTABLE" ]; then
        echo "Error: x265 executable not found. Please ensure the PGO generation phase completed successfully."
        exit 1
    fi
    
    # Run coverage report
    echo "----- File Coverage Report -----"
    llvm-cov report "$X265_EXECUTABLE" -instr-profile="$PROFILE_DATA_PATH"
    
    echo ""
    echo "Note: Files with 0% coverage will generate '-Wprofile-instr-unprofiled' warnings during build."
    echo "These files won't benefit from PGO optimization."
    
    echo ""
    echo "=== Step 5: Cleaning temporary build ==="
    cd "${TOP_DIR}/10bit"
    make clean
    
    echo "=== Step 6: Rebuilding actual 10bit library with performance data ==="

    cmake -G "Unix Makefiles" ../../../source -DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=OFF \
          -DENABLE_SHARED=OFF -DENABLE_CLI=OFF \
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
          -DCMAKE_C_FLAGS="${CLANG_PROFILE_USE}${PROFILE_DATA_PATH}" \
          -DCMAKE_CXX_FLAGS="${CLANG_PROFILE_USE}${PROFILE_DATA_PATH}" \
          -DCMAKE_EXE_LINKER_FLAGS="${CLANG_PROFILE_USE}${PROFILE_DATA_PATH}" \
          -DCMAKE_SHARED_LINKER_FLAGS="${CLANG_PROFILE_USE}${PROFILE_DATA_PATH}"
    
    make ${MAKEFLAGS}
    
    echo "Note: If you see '-Wprofile-instr-unprofiled' warnings, it means some source files didn't collect profile data."
    echo "This won't prevent the build, but will reduce PGO optimization effectiveness for those files."
else
    # Standard 10bit build (no PGO)
    cmake -G "Unix Makefiles" ../../../source -DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=OFF \
          -DENABLE_SHARED=OFF -DENABLE_CLI=OFF
    make ${MAKEFLAGS}
fi

cp libx265.a ../8bit/libx265_main10.a

# ======= 8bit Build =======
echo "Building 8bit library and combining final library..."
cd "${TOP_DIR}/8bit"
cmake -G "Unix Makefiles" ../../../source -DEXTRA_LIB="x265_main10.a;x265_main12.a" -DEXTRA_LINK_FLAGS=-L. -DLINKED_10BIT=ON -DLINKED_12BIT=ON
make ${MAKEFLAGS}

mv libx265.a libx265_main.a

uname=`uname`
if [ "$uname" = "Linux" ]
then

# On Linux, we use GNU ar to combine the static libraries together
ar -M <<EOF
CREATE libx265.a
ADDLIB libx265_main.a
ADDLIB libx265_main10.a
ADDLIB libx265_main12.a
SAVE
END
EOF

else

# Mac/BSD libtool
libtool -static -o libx265.a libx265_main.a libx265_main10.a libx265_main12.a 2>/dev/null

fi

echo "===== Build Complete ====="
if $ENABLE_10BIT_PGO; then
    echo "Successfully built 10bit multilib version with PGO optimization!"
else
    echo "Successfully built standard multilib version!"
fi