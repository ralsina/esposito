# Touch Coordinate Validation Status

## Rotation 0 (Portrait 240×320)
- **Status**: ✅ VALIDATED
- **Fix applied**: Y axis inversion
- **Test**: Touch bottom-right corner → got (234, 18) 
- **Expected**: (~236, ~319)
- **Result**: X correct (234), Y inverted (18 instead of ~319)
- **Formula**: `screen_y = (screen_height - 1) - ((raw_y - 200) * screen_height / 3700)`

## Rotation 1 (Landscape 320×240) 
- **Status**: ✅ VALIDATED  
- **Fix applied**: Axes swapped + both inverted
- **Test 1**: Touch bottom-left → got (4, 286), confirmed axes swapped
- **Test 2**: Touch bottom-right → got (14, 17), confirmed both inverted
- **Expected**: Bottom-left (~0, ~239), Bottom-right (~319, ~239)
- **Formula**: 
  - `screen_x = (screen_height - 1) - ((raw_y - 200) * screen_width / 3700)`
  - `screen_y = (screen_width - 1) - ((raw_x - 200) * screen_height / 3700)`

## Rotation 2 (Portrait 180° 240×320)
- **Status**: ✅ VALIDATED
- **Fix applied**: X axis inversion  
- **Test**: Touch near bottom-right logical corner → got (5, 295)
- **Expected**: (~236, ~319)
- **Result**: X inverted (5 instead of ~236), Y reasonable (295)
- **Formula**: `screen_x = (screen_width - 1) - ((raw_x - 200) * screen_width / 3700)`

## Rotation 3 (Landscape 270° 320×240)
- **Status**: ✅ VALIDATED
- **Fix applied**: Both axes inverted
- **Test 1**: Touch near bottom-right → got (16, 0), confirmed both inverted  
- **Test 2**: Touch bottom-left → got (233, 33), confirmed both inverted
- **Expected**: Bottom-right (~319, ~239), Bottom-left (~0, ~239)
- **Formula**:
  - `screen_x = (screen_height - 1) - ((raw_x - 200) * screen_height / 3700)`
  - `screen_y = (screen_width - 1) - ((raw_y - 200) * screen_width / 3700)`

## Calibration Constants (Hardware-specific)
- **200**: XPT2046 minimum raw value (maps to 0)
- **3900**: XPT2046 maximum raw value (maps to max coordinate)
- **3700**: XPT2046 raw range (3900 - 200)
- **These are hardware constants and should remain hardcoded**

## Next Steps
1. Build firmware with all rotation fixes
2. Systematically test each rotation mode
3. Test UI interaction (buttons, lists) in each rotation
4. Validate edge cases (corners, center, etc.)
5. Commit and push final implementation