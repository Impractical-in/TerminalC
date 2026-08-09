class Terminalc < Formula
  desc "Arbitrary-precision terminal calculator"
  homepage "https://github.com/Impractical-in/TerminalC"
  url "https://github.com/Impractical-in/TerminalC/archive/refs/heads/main.tar.gz"
  version "1.0.0-beta.1"
  head "https://github.com/Impractical-in/TerminalC.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "gmp"
  depends_on "mpfr"

  def install
    prefix_path = "#{Formula[\"gmp\"].opt_prefix};#{Formula[\"mpfr\"].opt_prefix}"
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
           "-DTERMINALC_ENABLE_LTO=ON", "-DCMAKE_PREFIX_PATH=#{prefix_path}"
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build", "--prefix", prefix
  end

  test do
    assert_match "1", shell_output("#{bin}/terminalc --no-color --eval 'real(ifft(fft([1,2,3,4])))'")
  end
end
