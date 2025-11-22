# first source the chipyard environment
source /home/ishara/Research/repos/chipyard/env.sh

# compilation variables
CC=riscv64-unknown-elf-gcc
MARCH=rv64gc
MABI=lp64d
CFLAGS="-mcmodel=medany -O2 -std=gnu11"
SOURCE_FILES=(
    ../src/main/resources/genesys2/sdboot/hello.c
)
# SOURCE_FILES=(
#   ../src/main/resources/genesys2/sdboot/decoder.c
#   ../src/main/resources/genesys2/sdboot/jpg.c
#   ../src/main/resources/genesys2/sdboot/embedded_cat.c
# )
OUTPUT_FILE=output.elf

# then compile the hello world program
${CC} \
  -march=${MARCH} -mabi=${MABI} ${CFLAGS} \
  "${SOURCE_FILES[@]}" \
  -o ${OUTPUT_FILE}

# run it on spike to verify
spike pk ${OUTPUT_FILE}