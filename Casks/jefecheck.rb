cask "jefecheck" do
  version "1.7.2"
  sha256 "38dc1084618ed19c7602389ee72062ec63353f4a22544351d5f58d222943071e"

  url "https://github.com/danielgollas/jefecheck/releases/download/v#{version}/jefecheck-macos-arm64.dmg"
  name "JefeCheck"
  desc "Video frame processing and playback for color correction"
  homepage "https://github.com/danielgollas/jefecheck"

  livecheck do
    url :url
    strategy :github_latest
  end

  depends_on macos: ">= :big_sur"
  depends_on arch: :arm64

  app "JefeCheck.app"

  zap trash: [
    "~/Library/Application Support/JefeCheck",
    "~/Library/Preferences/com.danielgollas.jefecheck.plist",
  ]
end
