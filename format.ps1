./src.ps1 | Select-String -NotMatch "CxCli.h" | Select-String -NotMatch "dotnet" | ForEach-Object {
  clang-format -verbose -style=file -i $_
}
