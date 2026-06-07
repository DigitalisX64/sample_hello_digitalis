// Copyright (C) 2026 utzcoz
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Minimal Qt 6 Widgets application for the Digitalis sample suite.
//
// This exercises the Qt-on-Android native stack under Berberis ARM64->x86_64
// translation: the in-APK Qt shared libraries (libQt6Core/Gui/Widgets) and,
// crucially, the Qt Android platform plugin
// (libplugins_platforms_qtforandroid_arm64-v8a.so) which bridges Qt's event
// loop and rendering to the Android Activity / SurfaceView. This is the same
// code path that VulkanCapsViewer (a Qt 6 prebuilt regression target) loads.
//
// The app brings up a QApplication, builds a tiny widget tree (a label and a
// button) and shows it full-screen. Reaching a visible, running event loop is
// the spec: it means QtCore (event dispatcher, object system), QtGui (the
// platform integration, EGL/window surface) and the qtforandroid plugin all
// loaded and translated correctly.

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  QWidget window;
  window.setWindowTitle(QStringLiteral("hello-qt"));

  auto* layout = new QVBoxLayout(&window);

  auto* label = new QLabel(QStringLiteral("hello-qt"), &window);
  label->setAlignment(Qt::AlignCenter);
  QFont font = label->font();
  font.setPointSize(32);
  label->setFont(font);
  layout->addWidget(label);

  auto* status = new QLabel(
      QStringLiteral("Qt platform plugin OK under Berberis"), &window);
  status->setAlignment(Qt::AlignCenter);
  status->setStyleSheet(QStringLiteral("color: green;"));
  layout->addWidget(status);

  auto* button = new QPushButton(QStringLiteral("Tap"), &window);
  int counter = 0;
  QObject::connect(button, &QPushButton::clicked, &window, [label, counter]() mutable {
    ++counter;
    label->setText(QStringLiteral("hello-qt (%1)").arg(counter));
  });
  layout->addWidget(button);

  window.showFullScreen();
  return app.exec();
}
