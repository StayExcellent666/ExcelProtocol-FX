#include "raid-audio.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 || !QFileInfo::exists(QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "A generated WAV path is required\n";
        return 1;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    // Gain zero keeps this device/lifetime stress test silent while still
    // exercising open, prepare, reset, unprepare, and close repeatedly.
    for (int cycle = 0; cycle < 100; ++cycle) {
        fx::RaidAudio audio;
        audio.setVolume(100);
        audio.play(path, "weapon", 0.0, 0);
        audio.play(path, "enemy", 0.0, 1);
        audio.play(path, "effect", 0.0, 2);
        audio.play(path, "stinger", 0.0, 4);
        audio.stop();
    }
    return 0;
}
