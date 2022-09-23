# 编译

* 启动编译容器进行编译
```

cd iio-oscilloscope
docker run -it -v $PWD:/iio-oscilloscope -w /iio-oscilloscope panling51/oscilloscopebuild bash
./CI/appveyor/build.sh x86_64

```

# 发布

* 下载安装innosetup-6.2.1

* 编译发布版本
  
因在wsl2直接运行updated-osc.iss会出现路径问题, 所以需要将updated-osc.iss, license.txt, sysroot-x86_64拷贝到windows目录,
然后直接点击运行updated-osc.iss, 会在编译目录下生成aiclab-osc-setup