Pod::Spec.new do |s|
  s.name             = 'texture_demo'
  s.version          = '0.1.0'
  s.summary          = 'Draws into GPU buffers and shows them through sofluffy_texture.'
  s.homepage         = 'https://github.com/SoFluffyOS/sofluffy_texture'
  s.license          = { :type => 'BSD-3-Clause' }
  s.author           = { 'SoFluffy' => 'https://github.com/SoFluffyOS' }
  s.source           = { :path => '.' }
  s.source_files     = 'texture_demo/Sources/texture_demo/**/*.swift'
  s.dependency 'FlutterMacOS'
  s.dependency 'sofluffy_texture'

  s.platform = :osx, '10.15'
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
  s.swift_version = '5.0'
end
