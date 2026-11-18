#
# The macOS plugin for CocoaPods apps; sofluffy_texture/Package.swift serves
# Swift Package Manager apps from the same sources.
#
Pod::Spec.new do |s|
  s.name             = 'sofluffy_texture'
  s.version          = '0.1.0'
  s.summary          = 'Hands native GPU buffers to Flutter as textures.'
  s.description      = <<-DESC
IOSurfaces and CVPixelBuffers that native code already holds, shown as a
Flutter texture without a copy. Other plugins call its C API in-process.
                       DESC
  s.homepage         = 'https://github.com/SoFluffyOS/sofluffy_texture'
  s.license          = { :type => 'BSD-3-Clause', :file => '../LICENSE' }
  s.author           = { 'SoFluffy' => 'https://github.com/SoFluffyOS' }
  s.source           = { :path => '.' }
  s.source_files     = 'sofluffy_texture/Sources/sofluffy_texture/**/*.{h,mm}'
  s.public_header_files = 'sofluffy_texture/Sources/sofluffy_texture/include/**/*.h'
  s.dependency 'FlutterMacOS'
  s.frameworks       = 'CoreVideo', 'IOSurface'

  s.platform = :osx, '10.15'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
  }
end
