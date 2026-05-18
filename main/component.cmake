# Component-specific compile flags for single-precision floats

idf_component_get_property(comp_targets main)

# Add compiler flags for single-precision float literals
target_compile_options(${comp_targets} PRIVATE
    "-fsingle-precision-constant"
    "-Wdouble-promotion"
)
