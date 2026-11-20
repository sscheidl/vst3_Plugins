#pragma once

#define stringCompanyName "TAUREON"
#define stringCompanyWeb ""
#define stringCompanyEmail ""

// The product name, after Charles Messier and his catalogue of nebulae. Plain
// ASCII: the same text serves the class infos, the bundle and the window title.
#define stringPluginName "Messier"

// The release, as a tag and the documents name it. Keep equal to ../components.json; its
// numbers (without the pre-release part) to ../CMakeLists.txt.
#define FULL_VERSION_STR "1.0.0-beta.1"
// As a person reads it: in the window.
#define DISPLAY_VERSION_STR "1.0 beta 1"
// The class version the host reads: Major.Minor.Subversion, numbers only, as the SDK
// describes the field (ipluginbase.h). The pre-release part stays in the texts above.
#define CLASS_VERSION_STR "1.0.0"
