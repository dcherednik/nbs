OWNER(g:ymake)

PY23_TEST()

IF (PY2)
    TEST_SRCS(
        build_dll_and_java.py
        build_java_codenav_index.py
        build_java_with_error_prone.py
        build_java_with_error_prone2.py
        cat.py
        cgo1_wrapper.py
        check_config_h.py
        collect_java_srcs.py
        compile_cuda.py
        compile_java.py
        compile_jsrc.py
        compile_pysrc.py
        configure_file.py
        copy_docs_files.py
        copy_docs_files_to_dir.py
        copy_files_to_dir.py
        copy_to_dir.py
        coverage-info.py
        cpp_flatc_wrapper.py
        create_jcoverage_report.py
        extract_asrc.py
        extract_docs.py
        extract_jacoco_report.py
        f2c.py
        fail_module_cmd.py
        fetch_from.py
        fetch_from_external.py
        fetch_from_mds.py
        fetch_from_npm.py
        fetch_from_sandbox.py
        fetch_resource.py
        filter_zip.py
        find_and_tar.py
        fix_msvc_output.py
        fs_tools.py
        gen_aar_gradle_script.py
        gen_java_codenav_entry.py
        gen_java_codenav_protobuf.py
        gen_py3_reg.py
        gen_py_reg.py
        gen_test_apk_gradle_script.py
        gen_ub.py
        generate_pom.py
        go_proto_wrapper.py
        go_tool.py
        ios_wrapper.py
        java_pack_to_file.py
        link_asrc.py
        link_dyn_lib.py
        link_exe.py
        link_fat_obj.py
        link_lib.py
        llvm_opt_wrapper.py
        merge_coverage_data.py
        merge_files.py
        mkdir.py
        mkdocs_builder_wrapper.py
        mkver.py
        pack_ios.py
        pack_jcoverage_resources.py
        postprocess_go_fbs.py
        preprocess.py
        py_compile.py
        run_ios_simulator.py
        run_javac.py
        run_junit.py
        run_llvm_dsymutil.py
        run_msvc_wine.py
        run_tool.py
        sky.py
        stdout2stderr.py
        symlink.py
        tar_directory.py
        tar_sources.py
        tared_protoc.py
        touch.py
        unpacking_jtest_runner.py
        vcs_info.py
        with_coverage.py
        with_crash_on_timeout.py
        with_pathsep_resolve.py
        wrap_groovyc.py
        wrapper.py
        writer.py
        write_file_size.py
        xargs.py
        yield_line.py
        yndexer.py
    )
ELSEIF(PY3)
    STYLE_PYTHON()

    TEST_SRCS(
        build_info_gen.py
        copy_clang_profile_rt.py
        gen_yql_python_udf.py
    )
ENDIF()

END()
