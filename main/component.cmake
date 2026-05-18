# Component-specific compile flags for single-precision floats

idf_component_get_property(comp_targets main)

# Add compiler flags for single-precision float literals
# Disable double-promotion warnings to avoid errors from logging macros
target_compile_options(${comp_targets} PRIVATE
    "-fsingle-precision-constant"
    "-Wno-double-promotion"
)
