#pragma once
namespace penchat_compositor {
// Transfer ownership once, before creating the S6 DisplaySurface. No fallback to VO.
void configureDisplay(int socket, int shared);
}
