/*
	Copyright (C) 2026 AERA Recovery Project contributors
	SPDX-License-Identifier: Apache-2.0
*/

package twrp

import (
	"fmt"
	"strconv"
	"strings"
)

// aeraVariable names one build variable. Device trees set the public AERA_*
// spelling; aera_config.mk maps it to the internal TW_*/OF_* name consumed by
// the recovery backend. Either spelling may be visible to Soong depending on
// which make variables are exported.
type aeraVariable struct {
	public   string
	internal string
}

func (v aeraVariable) lookupNames() []string {
	if v.public == "" {
		return []string{v.internal}
	}
	return []string{v.public, v.internal}
}

func (v aeraVariable) describe() string {
	if v.public == "" || v.public == v.internal {
		return v.internal
	}
	return v.internal + " (set via " + v.public + ")"
}

// Variables that must hold a decimal integer. This mirrors the numeric flag
// table in YARP's twrp_flags.go so device trees fail early instead of passing
// garbage to the C++ compiler.
var aeraIntegerVariables = []aeraVariable{
	{internal: "BOARD_BOOT_HEADER_VERSION"},
	{public: "AERA_CUSTOM_POWER_BUTTON", internal: "TW_CUSTOM_POWER_BUTTON"},
	{public: "AERA_USE_KEY_CODE_TOUCH_SYNC", internal: "TW_USE_KEY_CODE_TOUCH_SYNC"},
	{public: "AERA_QCOM_ATS_OFFSET", internal: "TW_QCOM_ATS_OFFSET"},
	{public: "AERA_CLOCK_OFFSET", internal: "TW_CLOCK_OFFSET"},
	// 1 selects the FBE policy used by libtar/libvold; 2 is the default.
	{public: "AERA_USE_FSCRYPT_POLICY", internal: "TW_USE_FSCRYPT_POLICY"},
}

// Variables that are switches. Only 0/1/true/false (optionally quoted) are
// valid; anything else is almost certainly a typo.
var aeraBooleanVariables = []aeraVariable{
	{public: "AERA_INCLUDE_CRYPTO", internal: "TW_INCLUDE_CRYPTO"},
	{public: "AERA_INCLUDE_CRYPTO_FBE", internal: "TW_INCLUDE_CRYPTO_FBE"},
	{public: "AERA_INCLUDE_FBE_METADATA_DECRYPT", internal: "TW_INCLUDE_FBE_METADATA_DECRYPT"},
	{public: "AERA_INCLUDE_OMAPI", internal: "TW_INCLUDE_OMAPI"},
	{internal: "AERA_VENDOR_BOOT_RECOVERY"},
	{internal: "AERA_AB_DEVICE"},
	{internal: "AERA_VIRTUAL_AB_DEVICE"},
	{internal: "AERA_VANILLA_BUILD"},
	{public: "AERA_AB_DEVICE_WITH_RECOVERY_PARTITION", internal: "OF_AB_DEVICE_WITH_RECOVERY_PARTITION"},
	{public: "AERA_USE_DMCTL", internal: "OF_USE_DMCTL"},
	{internal: "AERA_USE_DMSETUP"},
	{public: "AERA_ENABLE_WLAN", internal: "OF_ENABLE_WLAN"},
	{public: "AERA_ENABLE_LAB", internal: "OF_ENABLE_LAB"},
	{public: "AERA_USE_NANO_EDITOR", internal: "OF_USE_NANO_EDITOR"},
	{public: "AERA_ALLOW_EARLY_SETTINGS_LOAD", internal: "OF_ALLOW_EARLY_SETTINGS_LOAD"},
	{public: "AERA_USE_DATA_RECOVERY_FOR_SETTINGS", internal: "OF_USE_DATA_RECOVERY_FOR_SETTINGS"},
	{public: "AERA_SUPPORT_OZIP_DECRYPTION", internal: "OF_SUPPORT_OZIP_DECRYPTION"},
	{public: "AERA_USE_MEIZU_TOUCH_MAPPING", internal: "OF_USE_MEIZU_TOUCH_MAPPING"},
}

// Switches that are only meaningful when another switch is enabled. The Make
// build silently ignores these combinations, which used to produce images
// missing the requested feature.
var aeraDependencies = []struct {
	enabled  aeraVariable
	requires aeraVariable
}{
	{
		enabled:  aeraVariable{public: "AERA_INCLUDE_OMAPI", internal: "TW_INCLUDE_OMAPI"},
		requires: aeraVariable{public: "AERA_INCLUDE_CRYPTO", internal: "TW_INCLUDE_CRYPTO"},
	},
	{
		enabled:  aeraVariable{public: "AERA_INCLUDE_FBE_METADATA_DECRYPT", internal: "TW_INCLUDE_FBE_METADATA_DECRYPT"},
		requires: aeraVariable{public: "AERA_INCLUDE_CRYPTO", internal: "TW_INCLUDE_CRYPTO"},
	},
}

var aeraVendorBootRecovery = aeraVariable{internal: "AERA_VENDOR_BOOT_RECOVERY"}

// Strip whitespace and one matching pair of outer quotes so make values such
// as `"true"` and ` 4 ` are validated like their plain forms.
func aeraScalar(value string) string {
	value = strings.TrimSpace(value)
	if len(value) >= 2 && value[0] == '"' && value[len(value)-1] == '"' {
		value = value[1 : len(value)-1]
	}
	return strings.TrimSpace(value)
}

func aeraLookup(getVar func(string) string, names ...string) (string, string) {
	for _, name := range names {
		if value := strings.TrimSpace(getVar(name)); value != "" {
			return name, value
		}
	}
	return "", ""
}

func aeraBoolValue(value string) (bool, error) {
	switch aeraScalar(value) {
	case "", "0", "false":
		return false, nil
	case "1", "true":
		return true, nil
	default:
		return false, fmt.Errorf("expected one of 0, 1, true, false, got %q", aeraScalar(value))
	}
}

func aeraSwitchState(getVar func(string) string, variable aeraVariable) (bool, error) {
	name, value := aeraLookup(getVar, variable.lookupNames()...)
	if name == "" {
		return false, nil
	}
	enabled, err := aeraBoolValue(value)
	if err != nil {
		return false, fmt.Errorf("%s: %v", variable.describe(), err)
	}
	return enabled, nil
}

func aeraProblems(problems []string) error {
	if len(problems) == 0 {
		return nil
	}
	return fmt.Errorf("invalid AERA build variables:\n%s", strings.Join(problems, "\n"))
}

func aeraTypeProblems(getVar func(string) string) []string {
	var problems []string
	for _, variable := range aeraIntegerVariables {
		name, value := aeraLookup(getVar, variable.lookupNames()...)
		if name == "" {
			continue
		}
		scalar := aeraScalar(value)
		if scalar == "" {
			continue
		}
		if _, err := strconv.ParseInt(scalar, 10, 64); err != nil {
			problems = append(problems, fmt.Sprintf("%s: expected a decimal integer, got %q", variable.describe(), scalar))
		}
	}
	for _, variable := range aeraBooleanVariables {
		if _, err := aeraSwitchState(getVar, variable); err != nil {
			problems = append(problems, err.Error())
		}
	}
	return problems
}

func aeraDependencyProblems(getVar func(string) string) []string {
	var problems []string
	for _, dependency := range aeraDependencies {
		enabled, err := aeraSwitchState(getVar, dependency.enabled)
		if err != nil {
			// Type validation reports the malformed value; there is nothing
			// meaningful to compare here.
			continue
		}
		if !enabled {
			continue
		}
		required, err := aeraSwitchState(getVar, dependency.requires)
		if err != nil {
			continue
		}
		if !required {
			problems = append(problems, fmt.Sprintf(
				"%s requires %s; enable %s or disable %s",
				dependency.enabled.describe(), dependency.requires.describe(),
				dependency.requires.describe(), dependency.enabled.describe()))
		}
	}

	enabled, err := aeraSwitchState(getVar, aeraVendorBootRecovery)
	if err == nil && enabled {
		if _, value := aeraLookup(getVar, "BOARD_BOOT_HEADER_VERSION"); value == "" {
			problems = append(problems, fmt.Sprintf(
				"%s requires BOARD_BOOT_HEADER_VERSION=4, but it is not set",
				aeraVendorBootRecovery.describe()))
		} else if version, err := strconv.Atoi(aeraScalar(value)); err != nil || version != 4 {
			problems = append(problems, fmt.Sprintf(
				"%s requires BOARD_BOOT_HEADER_VERSION=4, got %q",
				aeraVendorBootRecovery.describe(), aeraScalar(value)))
		}
	}
	return problems
}

func validateAeraVariableTypes(getVar func(string) string) error {
	return aeraProblems(aeraTypeProblems(getVar))
}

func validateAeraDependencies(getVar func(string) string) error {
	return aeraProblems(aeraDependencyProblems(getVar))
}

// validateAeraBuildVariables is the single entry point used by the Soong load
// hooks. It reports every problem at once so a device tree can be fixed in one
// pass instead of one build at a time.
func validateAeraBuildVariables(getVar func(string) string) error {
	problems := aeraTypeProblems(getVar)
	problems = append(problems, aeraDependencyProblems(getVar)...)
	return aeraProblems(problems)
}
