// FFCC port diagnostics: guest word watch (WIICOMPILED_WATCH=0xADDR[,0xADDR...]).
#pragma once
namespace FfccWatch {
bool Enabled();
void Poll(const char* site);  // logs "[watch] site: 0xADDR old -> new active=0x..." when a watched word changed
}
