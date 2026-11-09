class Moxy < Formula
  desc "C with modern conveniences and automatic memory management"
  homepage "https://github.com/wess/moxy"
  url "https://github.com/wess/moxy/archive/refs/tags/v0.2.2.tar.gz"
  sha256 "9cb25d3e9792289d132067451405e8bc0b46a76f849900b9a4ae8657145f49cb"
  license "MIT"

  head "https://github.com/wess/moxy.git", branch: "main"

  # source tarballs do not include submodules, so goose is pinned here
  resource "goose" do
    url "https://github.com/wess/goose/archive/ab6f2efab9e5b8af655a6bffa39017b9b1e7fda2.tar.gz"
    sha256 "436ebb7e90cc7c828dbd5bc10aa60369362d464f384e8ec8260b5277204b661f"
  end

  def install
    if build.head?
      system "git", "submodule", "update", "--init", "--recursive"
    else
      rm_r "libs/goose" if File.directory?("libs/goose")
      resource("goose").stage(buildpath/"libs/goose")
    end
    system "make", "CC=#{ENV.cc}"
    bin.install "build/moxy"
  end

  test do
    (testpath/"hello.mxy").write("void main() { print(40 + 2); }\n")
    assert_equal "42\n", shell_output("#{bin}/moxy run #{testpath}/hello.mxy")
  end
end
