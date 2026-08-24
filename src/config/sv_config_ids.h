#pragma once

// Control identifiers for the SoftVoice configuration utility.
//
// Every label carries its own id rather than sharing IDC_STATIC. A screen
// reader takes a control's accessible name from the static text immediately
// before it in the dialog template's z-order, and distinct ids make that
// pairing explicit and greppable instead of accidental.

#define IDI_APPICON 101
#define IDD_CONFIG 102

#define IDC_VOICE_LABEL 1000
#define IDC_VOICE 1001

#define IDC_RATE_LABEL 1002
#define IDC_RATE 1003
#define IDC_PITCH_LABEL 1004
#define IDC_PITCH 1005
#define IDC_VOLUME_LABEL 1006
#define IDC_VOLUME 1007
#define IDC_INFLECTION_LABEL 1008
#define IDC_INFLECTION 1009
#define IDC_BREATHINESS_LABEL 1010
#define IDC_BREATHINESS 1011
#define IDC_ROUGHNESS_LABEL 1012
#define IDC_ROUGHNESS 1013
#define IDC_VOWEL_LABEL 1014
#define IDC_VOWEL 1015

#define IDC_GLOTTAL_LABEL 1016
#define IDC_GLOTTAL 1017
#define IDC_INTONATION_LABEL 1018
#define IDC_INTONATION 1019
#define IDC_VOICING_LABEL 1020
#define IDC_VOICING 1021
#define IDC_GENDER_LABEL 1022
#define IDC_GENDER 1023
#define IDC_AVBIAS_LABEL 1024
#define IDC_AVBIAS 1025
#define IDC_MAKEUP_LABEL 1026
#define IDC_MAKEUP 1027

#define IDC_SAMPLERATE_LABEL 1028
#define IDC_SAMPLERATE 1029
#define IDC_LOGLEVEL_LABEL 1030
#define IDC_LOGLEVEL 1031

#define IDC_AUTOPREVIEW 1032
#define IDC_STATUS 1033

#define IDC_SPEAK 1040
#define IDC_RESET 1041
#define IDC_OPENLOGS 1042
