#include "image_data.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>

int main()
{
    char name[] = "/tmp/penchat-image-test-XXXXXX";
    const int fd = mkstemp(name);
    assert(fd >= 0);
    std::string output, error;
    const unsigned char jpeg[] = {0xff, 0xd8, 0xff};
    assert(write(fd, jpeg, sizeof(jpeg)) == sizeof(jpeg));
    assert(camera::imageDataUrl(name, &output, &error));
    assert(output == "data:image/jpeg;base64,/9j/");
    assert(lseek(fd, 0, SEEK_SET) == 0);
    const char png[] = "\x89PNG\r\n\x1a\n";
    assert(write(fd, png, 8) == 8);
    assert(camera::imageDataUrl(name, &output, &error));
    assert(output == "data:image/png;base64,iVBORw0KGgo=");
    assert(lseek(fd, 0, SEEK_SET) == 0);
    assert(write(fd, "BMabc", 5) == 5);
    assert(ftruncate(fd, 5) == 0);
    assert(camera::imageDataUrl(name, &output, &error));
    assert(output == "data:image/bmp;base64,Qk1hYmM=");
    assert(lseek(fd, 0, SEEK_SET) == 0);
    assert(write(fd, "RIFFabcdWEBP", 12) == 12);
    assert(camera::imageDataUrl(name, &output, &error));
    assert(output == "data:image/webp;base64,UklGRmFiY2RXRUJQ");
    assert(ftruncate(fd, 16 * 1024 * 1024 + 1) == 0);
    assert(!camera::imageDataUrl(name, &output, &error) && output.empty());
    assert(ftruncate(fd, 0) == 0);
    assert(!camera::imageDataUrl(name, &output, &error));
    assert(write(fd, "text", 4) == 4);
    assert(!camera::imageDataUrl(name, &output, &error));
    close(fd);
    unlink(name);
    assert(!camera::imageDataUrl(name, &output, &error));
    assert(!camera::imageDataUrl("/tmp", &output, &error));
    assert(!camera::imageDataUrl(std::string("x\0y", 3), &output, &error));
    puts("Image data tests passed");
}
