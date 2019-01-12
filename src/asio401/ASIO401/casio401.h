#pragma once

struct IASIO;

// Used by ASIO401Test to instantiate ASIO401 directly, instead of going through the ASIO Host SDK and COM.
// In production, standard COM factory mechanisms are used to instantiate ASIO401, not these functions.
IASIO * CreateASIO401();
void ReleaseASIO401(IASIO *);
