class Moxy < Formula
  desc "C with modern conveniences and automatic memory management"
  homepage "https://github.com/wess/moxy"
  url "https://github.com/wess/moxy/archive/refs/tags/v0.2.2.tar.gz"
  sha256 "9cb25d3e9792289d132067451405e8bc0b46a76f849900b9a4ae8657145f49cb"
  license "MIT"

  head "https://github.com/wess/moxy.git", branch: "main"

  # source tarballs do not include submodules, so goose is pinned here
  resource "goose" do
    url "https://github.com/wess/goose/archive/70460c29d4148141967b122e101ac8e763232d12.tar.gz"
    sha256 "f6040d02843d94c135ac9ed9baa2d58b61cf7ecb08854652639f186b5b425c7c"
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
