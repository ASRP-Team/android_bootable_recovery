/*
	Copyright (C) 2026 AERA Recovery Project contributors
	SPDX-License-Identifier: Apache-2.0
*/

package twrp

import (
	"strings"
	"testing"
)

func varsFrom(values map[string]string) func(string) string {
	return func(name string) string { return values[name] }
}

func TestValidateAeraVariableTypes(t *testing.T) {
	tests := []struct {
		name    string
		vars    map[string]string
		wantErr []string
	}{
		{
			name: "unset variables are valid",
		},
		{
			name: "valid integers and booleans",
			vars: map[string]string{
				"BOARD_BOOT_HEADER_VERSION":  "4",
				"AERA_CUSTOM_POWER_BUTTON":   `"116"`,
				"TW_USE_KEY_CODE_TOUCH_SYNC": "330",
				"TW_QCOM_ATS_OFFSET":         `"1642433544000"`,
				"AERA_CLOCK_OFFSET":          "-3600",
				"TW_USE_FSCRYPT_POLICY":      "2",
				"AERA_INCLUDE_OMAPI":         "1",
				"TW_INCLUDE_CRYPTO":          "true",
				"AERA_INCLUDE_CRYPTO_FBE":    `"false"`,
				"AERA_ENABLE_WLAN":           "0",
				"OF_ENABLE_LAB":              `""`,
			},
		},
		{
			name:    "integer with trailing text",
			vars:    map[string]string{"BOARD_BOOT_HEADER_VERSION": "4x"},
			wantErr: []string{"BOARD_BOOT_HEADER_VERSION"},
		},
		{
			name:    "integer with decimal point",
			vars:    map[string]string{"TW_CUSTOM_POWER_BUTTON": "116.0"},
			wantErr: []string{"TW_CUSTOM_POWER_BUTTON"},
		},
		{
			name:    "integer with embedded spaces",
			vars:    map[string]string{"TW_QCOM_ATS_OFFSET": "1642 433544000"},
			wantErr: []string{"TW_QCOM_ATS_OFFSET"},
		},
		{
			name:    "invalid boolean",
			vars:    map[string]string{"AERA_INCLUDE_OMAPI": "yes"},
			wantErr: []string{"AERA_INCLUDE_OMAPI"},
		},
		{
			name:    "uppercase boolean",
			vars:    map[string]string{"TW_INCLUDE_CRYPTO": "TRUE"},
			wantErr: []string{"TW_INCLUDE_CRYPTO"},
		},
		{
			name:    "numeric boolean out of range",
			vars:    map[string]string{"AERA_ENABLE_WLAN": "2"},
			wantErr: []string{"AERA_ENABLE_WLAN"},
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateAeraVariableTypes(varsFrom(test.vars))
			if len(test.wantErr) == 0 {
				if err != nil {
					t.Fatalf("validateAeraVariableTypes() = %v, want nil", err)
				}
				return
			}
			if err == nil {
				t.Fatal("validateAeraVariableTypes() = nil, want an error")
			}
			for _, name := range test.wantErr {
				if !strings.Contains(err.Error(), name) {
					t.Errorf("error %q does not name %s", err, name)
				}
			}
		})
	}
}

func TestValidateAeraDependencies(t *testing.T) {
	tests := []struct {
		name    string
		vars    map[string]string
		wantErr []string
	}{
		{
			name: "no dependent switches",
		},
		{
			name: "OMAPI with crypto",
			vars: map[string]string{"AERA_INCLUDE_OMAPI": "1", "AERA_INCLUDE_CRYPTO": "true"},
		},
		{
			name:    "OMAPI without crypto",
			vars:    map[string]string{"AERA_INCLUDE_OMAPI": "1"},
			wantErr: []string{"TW_INCLUDE_OMAPI", "TW_INCLUDE_CRYPTO"},
		},
		{
			name:    "OMAPI with crypto disabled",
			vars:    map[string]string{"TW_INCLUDE_OMAPI": "true", "TW_INCLUDE_CRYPTO": "false"},
			wantErr: []string{"TW_INCLUDE_OMAPI", "TW_INCLUDE_CRYPTO"},
		},
		{
			name: "OMAPI disabled without crypto",
			vars: map[string]string{"AERA_INCLUDE_OMAPI": "0"},
		},
		{
			name:    "metadata decrypt without crypto",
			vars:    map[string]string{"AERA_INCLUDE_FBE_METADATA_DECRYPT": "true"},
			wantErr: []string{"TW_INCLUDE_FBE_METADATA_DECRYPT", "TW_INCLUDE_CRYPTO"},
		},
		{
			name: "vendor boot with header version 4",
			vars: map[string]string{"AERA_VENDOR_BOOT_RECOVERY": "1", "BOARD_BOOT_HEADER_VERSION": "4"},
		},
		{
			name: "vendor boot with padded header version 4",
			vars: map[string]string{"AERA_VENDOR_BOOT_RECOVERY": "1", "BOARD_BOOT_HEADER_VERSION": ` "04" `},
		},
		{
			name:    "vendor boot with header version 3",
			vars:    map[string]string{"AERA_VENDOR_BOOT_RECOVERY": "1", "BOARD_BOOT_HEADER_VERSION": "3"},
			wantErr: []string{"AERA_VENDOR_BOOT_RECOVERY", "BOARD_BOOT_HEADER_VERSION"},
		},
		{
			name:    "vendor boot without header version",
			vars:    map[string]string{"AERA_VENDOR_BOOT_RECOVERY": "1"},
			wantErr: []string{"AERA_VENDOR_BOOT_RECOVERY", "BOARD_BOOT_HEADER_VERSION"},
		},
		{
			name: "vendor boot disabled with header version 3",
			vars: map[string]string{"AERA_VENDOR_BOOT_RECOVERY": "0", "BOARD_BOOT_HEADER_VERSION": "3"},
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateAeraDependencies(varsFrom(test.vars))
			if len(test.wantErr) == 0 {
				if err != nil {
					t.Fatalf("validateAeraDependencies() = %v, want nil", err)
				}
				return
			}
			if err == nil {
				t.Fatal("validateAeraDependencies() = nil, want an error")
			}
			for _, name := range test.wantErr {
				if !strings.Contains(err.Error(), name) {
					t.Errorf("error %q does not name %s", err, name)
				}
			}
		})
	}
}

func TestValidateAeraBuildVariables(t *testing.T) {
	legal := map[string]string{
		"BOARD_BOOT_HEADER_VERSION":         "4",
		"AERA_VENDOR_BOOT_RECOVERY":         "1",
		"AERA_INCLUDE_CRYPTO":               "true",
		"AERA_INCLUDE_CRYPTO_FBE":           "true",
		"AERA_INCLUDE_FBE_METADATA_DECRYPT": "true",
		"AERA_INCLUDE_OMAPI":                "true",
		"AERA_ENABLE_WLAN":                  "1",
		"AERA_USE_NANO_EDITOR":              "0",
		"AERA_CUSTOM_POWER_BUTTON":          "116",
		"AERA_CLOCK_OFFSET":                 "-3600",
		"TW_USE_FSCRYPT_POLICY":             "1",
	}
	if err := validateAeraBuildVariables(varsFrom(legal)); err != nil {
		t.Fatalf("legal configuration was rejected: %v", err)
	}

	broken := map[string]string{
		"BOARD_BOOT_HEADER_VERSION": "four",
		"AERA_INCLUDE_OMAPI":        "yes",
		"AERA_VENDOR_BOOT_RECOVERY": "1",
	}
	err := validateAeraBuildVariables(varsFrom(broken))
	if err == nil {
		t.Fatal("broken configuration was accepted")
	}
	for _, name := range []string{"BOARD_BOOT_HEADER_VERSION", "TW_INCLUDE_OMAPI", "AERA_VENDOR_BOOT_RECOVERY"} {
		if !strings.Contains(err.Error(), name) {
			t.Errorf("error %q does not name %s", err, name)
		}
	}
}
