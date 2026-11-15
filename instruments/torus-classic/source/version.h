#pragma once

#define stringCompanyName "TAUREON"
#define stringCompanyWeb ""
#define stringCompanyEmail ""

// The product name, "Kekule" with an acute accent on the last e: after August
// Kekule, who found the benzene ring (Rings, ring, Kekule). "Torus Classic" is
// the internal name of the project only: directory, branch, build targets,
// namespaces; see README.md.
//
// The name exists three times. Plain ASCII for the 8-bit class info, where an
// accent has no defined encoding; UTF-16 for the Unicode class info that
// current hosts read (see factory.cpp); UTF-8 for the editor's title. The
// escapes keep this file ASCII, like all code here.
#define stringPluginName "Kekule"
#define stringPluginNameUtf16 u"Kekul\x00E9"
#define stringPluginNameUtf8 "Kekul\xC3\xA9"

// Keep equal to the version in ../CMakeLists.txt and ../components.json.
#define FULL_VERSION_STR "0.2.2"
